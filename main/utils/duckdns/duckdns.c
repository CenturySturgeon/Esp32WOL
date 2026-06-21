#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_client.h"

#include "duckdns.h"
#include "../nvs/nvs_utils.h"

extern const char duckdns_pem_start[] asm("_binary_duckdns_pem_start");

static const char *TAG = "DUCKDNS";

#define DUCKDNS_RETRY_START_MS 5000
#define DUCKDNS_RETRY_MAX_MS 60000

esp_err_t duckdns_update_sync(const char *ip)
{
    char token[64] = {0};
    char domain[64] = {0};
    char url[256] = {0};

    if (nvs_get_duckdns_secrets(token, sizeof(token),
                                domain, sizeof(domain)) != ESP_OK)
    {
        return ESP_FAIL;
    }

    snprintf(url, sizeof(url),
             "https://www.duckdns.org/update?domains=%s&token=%s&ip=%s",
             domain, token, ip);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .cert_pem = duckdns_pem_start,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return ESP_FAIL;

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300)
        {
            ESP_LOGI(TAG, "DuckDNS updated (HTTP %d)", status);
        }
        else
        {
            ESP_LOGW(TAG, "DuckDNS API error (HTTP %d)", status);
            err = ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "DuckDNS request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    memset(token, 0, sizeof(token));
    memset(domain, 0, sizeof(domain));

    return err;
}

esp_err_t duckdns_update_with_retry(const char *ip)
{
    uint32_t delay_ms = DUCKDNS_RETRY_START_MS;

    while (1)
    {
        if (duckdns_update_sync(ip) == ESP_OK)
            return ESP_OK;

        ESP_LOGW(TAG, "Retrying DuckDNS in %lu ms...", delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        delay_ms *= 2;
        if (delay_ms > DUCKDNS_RETRY_MAX_MS)
            delay_ms = DUCKDNS_RETRY_MAX_MS;
    }
}
