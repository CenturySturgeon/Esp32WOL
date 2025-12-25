#include <string.h>
#include <stdbool.h>

#include "telegram.h"
#include "../../auth/auth.h"

#include "esp_log.h"
#include "esp_http_client.h"

extern const char telegram_pem_start[] asm("_binary_telegram_pem_start");
extern const char telegram_pem_end[] asm("_binary_telegram_pem_end");

static const char *TAG = "TELEGRAM";

// Internal synchronous sender (The original function, made static)
esp_err_t send_telegram_message_sync(const char *message, bool silent)
{
    char token[64] = {0};
    char chat_id[20] = {0};
    char post_data[512] = {0};
    char url[128] = {0};

    // Fetch secrets
    if (nvs_get_telegram_secrets(token, sizeof(token), chat_id, sizeof(chat_id)) != ESP_OK)
    {
        return ESP_FAIL;
    }

    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", token);

    // Ensure JSON safety (basic check, ideally use a JSON library or escape quotes)
    snprintf(post_data, sizeof(post_data),
             "{\"chat_id\":\"%s\",\"text\":\"%s\",\"disable_notification\":%s}",
             chat_id,
             message,
             silent ? "true" : "false");

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .cert_pem = telegram_pem_start,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
        return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300)
        {
            ESP_LOGI(TAG, "Telegram sent successfully (HTTP %d)", status);
        }
        else
        {
            ESP_LOGW(TAG, "Telegram sent but API returned error (HTTP %d)", status);
            err = ESP_FAIL; // Trigger retry on API error
        }
    }
    else
    {
        ESP_LOGE(TAG, "Telegram HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    // Security wipe
    memset(token, 0, sizeof(token));
    memset(chat_id, 0, sizeof(chat_id));

    return err;
}
