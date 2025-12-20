#include "wifi.h"
#include "freertos/task.h"
#include <string.h>

#include "../auth/auth.h"

static const char *TAG = "WIFI";

void ntp_sync_time(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    if (esp_sntp_enabled())
        esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;

    while (timeinfo.tm_year < (2016 - 1900) && ++retry)
    {
        ESP_LOGI(TAG, "Waiting for system time... (%d)", retry);
        vTaskDelay(pdMS_TO_TICKS(3000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year < (2016 - 1900))
    {
        ESP_LOGW(TAG, "Failed to obtain time via NTP");
        // Depending on your needs, you might want to return here
        // or allow the server to start with wrong time (will break HTTPS cert validation)
    }
    else
    {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "System time set to: %s", strftime_buf);
    }
}

void on_connected_task(void *pvParameters)
{
    // Sync time with NTP server (Critical for HTTPS)
    ntp_sync_time();

    // Delete task to avoid return errors
    vTaskDelete(NULL);
}


void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected. Retrying...");
        esp_wifi_connect();
        // Handle server cleanup if needed, or leave running
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got Local IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Placeholder for logic after successful connection
        xTaskCreate(on_connected_task, "startup_task", 8192, NULL, 5, NULL);
    }
}

/* -------- WIFI INIT -------- */
void wifi_init_sta(const char *ssid, const char *pass)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    
    // Copy credentials into the wifi_config struct safely
    if (ssid)
        strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass)
        strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // The driver has now copied the credentials to its internal memory.
}