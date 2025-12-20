#include "wifi.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include <string.h>

#include "../auth/auth.h"
#include "../web/server/server.h"

static const char *TAG = "WIFI";

static const uint32_t NTP_SYNC_INTERVAL_MS = 24 * 60 * 60 * 1000; // 24 hours in milliseconds
static const uint8_t NIGHT_START_HOUR_UTC = 1;
static const uint8_t NIGHT_END_HOUR_UTC = 5;
static const uint32_t NTP_MANDATORY_SYNC_MS = 3 * 24 * 60 * 60 * 1000; // 3 Days
static const uint32_t NTP_RETRY_DELAY_MS = 15 * 60 * 1000;             // 15 Mins (if mandatory fails)

// Global server handles so other parts of the code can access them
httpd_handle_t g_http_server = NULL;
httpd_handle_t g_https_server = NULL;

/* --- Private Functions --- */

static void time_init_utc(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
}

static bool is_utc_night_time(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    gmtime_r(&now, &timeinfo);

    int hour = timeinfo.tm_hour;

    if (NIGHT_START_HOUR_UTC <= NIGHT_END_HOUR_UTC)
    {
        return (hour >= NIGHT_START_HOUR_UTC && hour < NIGHT_END_HOUR_UTC);
    }
    else
    {
        return (hour >= NIGHT_START_HOUR_UTC || hour < NIGHT_END_HOUR_UTC);
    }
}

static void ntp_management_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting NTP Management Task");
    time_init_utc();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // 1. Mandatory initial sync
    bool initial_sync_done = false;
    while (!initial_sync_done)
    {
        time_t now;
        struct tm timeinfo;
        time(&now);
        gmtime_r(&now, &timeinfo);

        if (timeinfo.tm_year >= (2020 - 1900))
        {
            char time_str[32];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
            ESP_LOGI(TAG, "Initial UTC time acquired: %s. HTTPS is now safe to use.", time_str);
            initial_sync_done = true;

            // --- START SERVERS AFTER INITIAL NTP SYNC ---
            ESP_LOGI(TAG, "Starting HTTP redirect server...");
            g_http_server = start_http_redirect_server();
            if (g_http_server == NULL)
            {
                ESP_LOGE(TAG, "Failed to start HTTP redirect server!");
            }

            ESP_LOGI(TAG, "Starting HTTPS server...");
            g_https_server = start_https_server();
            if (g_https_server == NULL)
            {
                ESP_LOGE(TAG, "Failed to start HTTPS server!");
            }
        }
        else
        {
            ESP_LOGI(TAG, "Waiting for mandatory initial NTP sync...");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    TickType_t last_success_tick = xTaskGetTickCount();

    // 2. Maintenance loop
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10 * 60 * 1000)); // 10 mins

        TickType_t current_tick = xTaskGetTickCount();
        TickType_t elapsed_ms = (current_tick - last_success_tick) * portTICK_PERIOD_MS;

        bool is_night = is_utc_night_time();
        bool interval_reached = (elapsed_ms >= NTP_SYNC_INTERVAL_MS);
        bool expired_3_days = (elapsed_ms >= NTP_MANDATORY_SYNC_MS);

        if ((interval_reached && is_night) || expired_3_days)
        {
            if (expired_3_days)
            {
                ESP_LOGW(TAG, "Time expired (3 days). Forcing mandatory sync now.");
            }
            else
            {
                ESP_LOGI(TAG, "Maintenance window: Scheduled night sync.");
            }

            sntp_restart();
            vTaskDelay(pdMS_TO_TICKS(10000));

            time_t now;
            struct tm timeinfo;
            time(&now);
            gmtime_r(&now, &timeinfo);

            if (timeinfo.tm_year >= (2020 - 1900))
            {
                ESP_LOGI(TAG, "NTP Sync successful.");
                last_success_tick = xTaskGetTickCount();
            }
            else
            {
                ESP_LOGE(TAG, "NTP Sync failed. Will retry in next check.");
                if (expired_3_days)
                {
                    vTaskDelay(pdMS_TO_TICKS(NTP_RETRY_DELAY_MS));
                }
            }
        }
    }
}

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected. Retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        static bool ntp_task_started = false;
        if (!ntp_task_started)
        {
            ntp_task_started = true;
            xTaskCreate(ntp_management_task, "ntp_mgr", 4096, NULL, 5, NULL);
        }
    }
}

void wifi_init_sta(const char *ssid, const char *pass)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Using PSK for stability
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    if (ssid)
        strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass)
        strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}