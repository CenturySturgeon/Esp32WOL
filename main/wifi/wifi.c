#include "wifi.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include <string.h>

#include "../auth/auth.h"
#include "../web/server/server.h"
#include "../utils/utils.h"

static const char *TAG = "WIFI";

// NTP Config
static const uint32_t NTP_SYNC_INTERVAL_MS = 24 * 60 * 60 * 1000; // 24 hours
static const uint8_t NIGHT_START_HOUR_UTC = 1;
static const uint8_t NIGHT_END_HOUR_UTC = 5;
static const uint32_t NTP_MANDATORY_SYNC_MS = 3 * 24 * 60 * 60 * 1000; // 3 Days
static const uint32_t NTP_RETRY_DELAY_MS = 15 * 60 * 1000;             // 15 Mins

// Public IP Config
static const uint32_t IP_CHECK_INTERVAL_MS = 20 * 60 * 1000; // 20 Mins
static const uint32_t IP_BOOT_RETRY_MS = 5 * 1000;           // 10 Secs (Fast retry on boot)

char public_ip[64];

// Global server handles
httpd_handle_t g_http_server = NULL;
httpd_handle_t g_https_server = NULL;

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

static void public_ip_management_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Public IP Management Task");

    char temp_ip[64]; // Temporary buffer to check for changes

    // Mandatory Boot Retrieval
    // Loop until we get the IP, blocking further IP logic but not crashing the system.
    bool initial_ip_got = false;
    while (!initial_ip_got)
    {
        ESP_LOGI(TAG, "Attempting to retrieve Public IP (Boot Requirement)...");
        if (get_public_ip(public_ip, sizeof(public_ip)) == ESP_OK)
        {
            ESP_LOGI(TAG, "SUCCESS: Public IP acquired: %s", public_ip);
            initial_ip_got = true;
        }
        else
        {
            ESP_LOGW(TAG, "Failed to get Public IP. Retrying in %lu ms...", IP_BOOT_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(IP_BOOT_RETRY_MS));
        }
    }

    // Periodic Lookup Loop
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(IP_CHECK_INTERVAL_MS));

        ESP_LOGI(TAG, "Performing periodic Public IP check...");

        // Use temp buffer to avoid overwriting global with bad data on failure
        if (get_public_ip(temp_ip, sizeof(temp_ip)) == ESP_OK)
        {
            // Compare new IP with stored IP
            if (strcmp(public_ip, temp_ip) != 0)
            {
                ESP_LOGW(TAG, "DETECTED CHANGE: Public IP changed from %s to %s", public_ip, temp_ip);

                // Update global variable
                memset(public_ip, 0, sizeof(public_ip));
                strncpy(public_ip, temp_ip, sizeof(public_ip) - 1);

                // TODO: Insert Notification Function Here
                // notify_user_of_ip_change(public_ip);
            }
            else
            {
                ESP_LOGI(TAG, "Public IP has not changed.");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Periodic IP check failed. Keeping last known IP.");
        }
    }
}

static void ntp_management_task(void *pvParameters)
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
            // We only start NTP manager. The IP manager is spawned BY the NTP manager
            // once time is safe for HTTPS.
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