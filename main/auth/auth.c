#include "auth.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"

#include "totp.h"
#include "../web/server/server.h"
#include "../utils/utils.h"

static const char *TAG = "AUTH_SYSTEM";

user_session_t *users_list = NULL;
uint8_t total_users_count = 0;

static uint8_t MAX_FAILED_LOGINS = 5;
static uint8_t failed_login_count = 0;
static SemaphoreHandle_t auth_mutex = NULL;

esp_err_t init_and_load_secrets()
{
    // Initialize NVS for the custom 'storage' partition
    // During production, you'll swap 'nvs_flash_init_partition'
    // for 'nvs_flash_secure_init_partition'
    esp_err_t err = nvs_flash_init_partition("storage");
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init storage partition: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    // Get total user count
    nvs_get_u8(handle, "total_users", &total_users_count);
    ESP_LOGI(TAG, "Total users found in NVS: %d", total_users_count);

    if (total_users_count > 0)
    {
        users_list = malloc(sizeof(user_session_t) * total_users_count);

        for (int i = 1; i <= total_users_count; i++)
        {
            char key[32];
            size_t required_size;

            // Load Name (Index is i-1 because loops starts at 1 for NVS keys)
            snprintf(key, sizeof(key), "user_%d_name", i);
            required_size = sizeof(users_list[i - 1].name);
            nvs_get_str(handle, key, users_list[i - 1].name, &required_size);

            // Load TTL
            snprintf(key, sizeof(key), "user_%d_TTL", i);
            nvs_get_u8(handle, key, &users_list[i - 1].ttl);

            // Load Hash
            snprintf(key, sizeof(key), "user_%d_hash", i);
            required_size = sizeof(users_list[i - 1].hash);
            nvs_get_str(handle, key, users_list[i - 1].hash, &required_size);

            // Load HMAC Binary Blob
            snprintf(key, sizeof(key), "user_%d_hmac", i);
            required_size = sizeof(users_list[i - 1].hmac);
            nvs_get_blob(handle, key, users_list[i - 1].hmac, &required_size);

            // ESP_LOGI(TAG, "Loaded [%s] - TTL: %d", users_list[i - 1].name, users_list[i - 1].ttl);
        }
    }

    ESP_LOGI(TAG, "System secrets loaded");

    auth_mutex = xSemaphoreCreateMutex();
    if (!auth_mutex)
    {
        ESP_LOGE(TAG, "Failed to create auth mutex");
        return ESP_FAIL;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t auth_get_wifi_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t handle;
    // Open the custom 'storage' partition
    esp_err_t err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    // Fetch SSID and Password
    err = nvs_get_str(handle, "wifi_ssid", ssid, &ssid_len);
    if (err == ESP_OK)
    {
        err = nvs_get_str(handle, "wifi_pass", pass, &pass_len);
    }

    nvs_close(handle);
    return err;
}

esp_err_t auth_get_telegram_secrets(char *token, size_t token_len, char *chat_id, size_t chat_id_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_get_str(handle, "bot_token", token, &token_len);
    if (err == ESP_OK)
    {
        err = nvs_get_str(handle, "chat_id", chat_id, &chat_id_len);
    }

    nvs_close(handle);
    return err;
}

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
            telegram_post_to_queue(msg, false);
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

void bytes_to_hex(const unsigned char *src, char *dest, int len)
{
    for (int i = 0; i < len; i++)
    {
        sprintf(dest + (i * 2), "%02x", src[i]);
    }
    dest[len * 2] = 0;
}

esp_err_t auth_login_user(const char *username, const char *password, char *out_token)
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
                return ESP_OK;
            }
            else
            {
                ESP_LOGW(TAG, "Session expired for user: %s", users_list[i].name);
                return ESP_FAIL;
            }
        }
    }
    return ESP_FAIL;
}

esp_err_t auth_get_user_hmac_via_token(
    const char *token,
    uint8_t *hmac_out,
    size_t *hmac_len)
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
        auth_register_failed_login();
        return ESP_FAIL;
    }
    else
    {

        bool valid = totp_verify(hmac_key, hmac_len, pin);

        if (!valid)
        {
            ESP_LOGE(TAG, "ACCESS DENIED");
            auth_register_failed_login();
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "TOTP Access allowed");

    return ESP_OK;
}
