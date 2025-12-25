#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"

#include "esp_random.h"

#include "auth.h"
#include "totp.h"
#include "../utils/utils.h"
#include "../web/server/server.h"
#include "../utils/telegram/queue.h"

static const char *TAG = "AUTH_SYSTEM";

static user_session_t *users_list = NULL;
static uint8_t total_users_count = 0;
// Instead of a simple true/false (which is easy to flip with 1 byte)
// Uses a specific multi-bit pattern.
#define AUTH_LOCKED_MAGIC 0x5A5A
#define AUTH_UNLOCKED_MAGIC 0xA5A5
static uint16_t auth_lock_status = AUTH_UNLOCKED_MAGIC;

static uint8_t MAX_FAILED_LOGINS = 5;
static uint8_t failed_login_count = 0;
static SemaphoreHandle_t auth_mutex = NULL;

static void auth_register_failed_login(void)
{
    xSemaphoreTake(auth_mutex, portMAX_DELAY);

    failed_login_count++;
    ESP_LOGW(TAG, "Failed login attempt %d/%d",
             failed_login_count, MAX_FAILED_LOGINS);

    if (failed_login_count >= MAX_FAILED_LOGINS)
    {
        if (https_server)
        {
            ESP_LOGE(TAG, "Max failed logins reached. Stopping HTTPS server.");
            char msg[128] = "🚨 Too Many Bad Login Attempts 🚨\nServer shutdown!";
            post_message_to_queue(msg, false);
            httpd_ssl_stop(https_server);
            https_server = NULL;
        }
    }

    xSemaphoreGive(auth_mutex);
}

static void auth_reset_failed_logins(void)
{
    xSemaphoreTake(auth_mutex, portMAX_DELAY);
    failed_login_count = 0;
    xSemaphoreGive(auth_mutex);

    ESP_LOGI(TAG, "Failed login counter reset");
}

static esp_err_t auth_get_user_hmac_via_token(const char *token, uint8_t *hmac_out, size_t *hmac_len)
{
    if (!token || !hmac_out || !hmac_len)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!users_list || total_users_count == 0)
    {
        return ESP_ERR_NOT_FOUND;
    }

    for (int i = 0; i < total_users_count; i++)
    {
        if (strcmp(users_list[i].session_token, token) == 0)
        {

            size_t stored_len = sizeof(users_list[i].hmac);

            if (*hmac_len < stored_len)
            {
                *hmac_len = stored_len;
                return ESP_ERR_NO_MEM;
            }

            memcpy(hmac_out, users_list[i].hmac, stored_len);
            *hmac_len = stored_len;

            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t auth_set_user_list(user_session_t *list, uint8_t count)
{
    // Prevent re-running after initialization (boot/reboot)
    if (auth_lock_status != AUTH_UNLOCKED_MAGIC)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (list == NULL && count > 0)
        return ESP_ERR_INVALID_ARG;

    users_list = list;
    total_users_count = count;
    return ESP_OK;
}

esp_err_t auth_semaphore_init()
{
    if (auth_mutex != NULL)
    {
        return ESP_OK;
    }

    auth_mutex = xSemaphoreCreateMutex();
    if (auth_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create auth mutex");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t auth_logout_user(const char *token)
{
    if (!users_list || !token)
        return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(auth_mutex, portMAX_DELAY);

    for (int i = 0; i < total_users_count; i++)
    {
        if (strcmp(users_list[i].session_token, token) == 0)
        {
            memset(users_list[i].session_token, 0, sizeof(users_list[i].session_token));
            users_list[i].session_expiry = 0;

            ESP_LOGI(TAG, "User %s logged out successfully", users_list[i].name);

            xSemaphoreGive(auth_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(auth_mutex);
    ESP_LOGW(TAG, "No user found with token %s", token);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t auth_logout_all_users()
{
    if (!users_list)
        return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(auth_mutex, portMAX_DELAY);

    for (int i = 0; i < total_users_count; i++)
    {
        memset(users_list[i].session_token, 0, sizeof(users_list[i].session_token));
        users_list[i].session_expiry = 0;
    }

    xSemaphoreGive(auth_mutex);

    ESP_LOGI(TAG, "All users have been logged out");
    return ESP_OK;
}

esp_err_t auth_login_user(const char *username, const char *password, char *out_token, uint8_t *ttl_out)
{
    if (!users_list || !username || !password)
        return ESP_FAIL;

    // Hash the incoming password
    unsigned char sha_output[32];
    char hash_string[65];

    // Note: mbedtls_sha256 takes (input, length, output, is_224)
    mbedtls_sha256((const unsigned char *)password, strlen(password), sha_output, 0);
    bytes_to_hex(sha_output, hash_string, 32);

    // Find User and Compare
    for (int i = 0; i < total_users_count; i++)
    {
        if (strcmp(users_list[i].name, username) == 0)
        {

            // Compare Hashes
            if (strcmp(users_list[i].hash, hash_string) == 0)
            {
                ESP_LOGI(TAG, "Password match for %s", username);

                auth_reset_failed_logins();

                // Generate Session Token (Random Hex)
                uint8_t rand_bytes[16];
                esp_fill_random(rand_bytes, 16);
                bytes_to_hex(rand_bytes, users_list[i].session_token, 16);

                // Set Expiry (TTL is in seconds, convert to microseconds)
                // If stored TTL is 0 return
                if (users_list[i].ttl <= 0)
                {
                    return ESP_FAIL;
                }
                int ttl_sec = users_list[i].ttl;
                users_list[i].session_expiry = esp_timer_get_time() + (ttl_sec * 1000000LL);

                if (out_token)
                {
                    strcpy(out_token, users_list[i].session_token);
                }
                return ESP_OK;
            }
            else
            {
                ESP_LOGW(TAG, "Invalid password for %s", username);
                auth_register_failed_login();
                return ESP_FAIL;
            }
        }
    }

    ESP_LOGW(TAG, "User %s not found", username);
    auth_register_failed_login();
    return ESP_FAIL;
}

esp_err_t auth_check_session(const char *token)
{
    if (!users_list || !token)
        return ESP_FAIL;

    int64_t now = esp_timer_get_time();

    for (int i = 0; i < total_users_count; i++)
    {
        // Check if token matches
        if (strlen(users_list[i].session_token) > 0 &&
            strcmp(users_list[i].session_token, token) == 0)
        {

            // Check Expiry
            if (now < users_list[i].session_expiry)
            {
                // Optional: extend session on activity?
                // users_list[i].session_expiry = now + (users_list[i].ttl * 60 * 1000000LL);
                ESP_LOGI(TAG, "Session valid for user: %s", users_list[i].name);
                auth_logout_user(token);
                return ESP_OK;
            }
            else
            {
                ESP_LOGW(TAG, "Session expired for user: %s", users_list[i].name);
                auth_logout_user(token);
                return ESP_FAIL;
            }
        }
    }
    return ESP_FAIL;
}

esp_err_t auth_check_totp_request(const char *token, const uint32_t pin)
{
    if (auth_check_session(token) != ESP_OK)
    {
        auth_register_failed_login();
        return ESP_FAIL;
    }

    // ESP_LOGI(TAG, "Received PIN: %u", pin);

    uint8_t hmac_key[32];
    size_t hmac_len = sizeof(hmac_key);

    esp_err_t err = auth_get_user_hmac_via_token(token, hmac_key, &hmac_len);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "User HMAC not found");
        auth_logout_user(token);
        auth_register_failed_login();
        return ESP_FAIL;
    }
    else
    {

        bool valid = totp_verify(hmac_key, hmac_len, pin);

        if (!valid)
        {
            ESP_LOGE(TAG, "TOTP request denied!");
            auth_logout_user(token);
            auth_register_failed_login();
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "TOTP Access allowed");

    return ESP_OK;
}
