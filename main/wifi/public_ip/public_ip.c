#include <stdbool.h>

#include "esp_http_client.h"
#include "esp_log.h"

#include "public_ip.h"
#include "../../utils/ipify/ipify.h"
#include "../../utils/led/led_utils.h"
#include "../../utils/telegram/queue.h"

static const char *TAG = "PUBLIC_IP";

// Public IP Config
static const uint32_t IP_CHECK_INTERVAL_MS = 20 * 60 * 1000; // 20 Mins
static const uint32_t IP_BOOT_RETRY_MS = 5 * 1000;           // 10 Secs (Fast retry on boot)

char public_ip[64] = {0};

void public_ip_management_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Public IP Management Task");

    led_utils_set_blinks(3);

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

            // Notify via Queue (note format string directly)
            post_message_to_queue("Esp32 online 🚀\nhttps://%s", true, public_ip);

            led_utils_set_blinks(0); // Turn off led
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

                post_message_to_queue("Public IP change to:\nhttps://%s", true, public_ip);
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