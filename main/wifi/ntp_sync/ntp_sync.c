#include <time.h>
#include <sys/time.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h" // Always first
#include "freertos/task.h"     // Then others
#include "freertos/semphr.h"   // (If you add a Mutex later)

#include "esp_log.h"
#include "esp_sntp.h"

#include "../../web/server/server.h"
#include "../public_ip/public_ip.h"

// NTP Config
static const uint32_t NTP_SYNC_INTERVAL_MS = 24 * 60 * 60 * 1000;      // 24 hours
static const uint8_t NIGHT_START_HOUR_UTC = 1;                         // 1AM UTC time
static const uint8_t NIGHT_END_HOUR_UTC = 5;                           // 5AM UTC time
static const uint32_t NTP_MANDATORY_SYNC_MS = 3 * 24 * 60 * 60 * 1000; // 3 Days
static const uint32_t NTP_RETRY_DELAY_MS = 15 * 60 * 1000;             // 15 Mins

static const char *TAG = "NTP_SYNC";

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

void ntp_management_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting NTP Management Task");
    time_init_utc();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Mandatory initial sync
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

            // Start Services Dependent on Time
            start_mdns_service();

            ESP_LOGI(TAG, "Starting HTTP redirect server...");
            http_server = start_http_redirect_server(); // Uses the server.c http server
            if (http_server == NULL)
            {
                ESP_LOGE(TAG, "Failed to start HTTP redirect server!");
            }

            ESP_LOGI(TAG, "Starting HTTPS server...");
            https_server = start_https_server(); // Uses the server.c https server
            if (https_server == NULL)
            {
                ESP_LOGE(TAG, "Failed to start HTTPS server!");
            }

            // Start Public IP Manager
            // Since get_public_ip uses HTTPS, it needs valid Time.
            xTaskCreate(public_ip_management_task, "ip_mgr", 8192, NULL, 5, NULL);
        }
        else
        {
            ESP_LOGI(TAG, "Waiting for mandatory initial NTP sync...");
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    TickType_t last_success_tick = xTaskGetTickCount();

    // Maintenance loop
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
