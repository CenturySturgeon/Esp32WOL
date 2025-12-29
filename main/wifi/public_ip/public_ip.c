// public_ip.c
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "public_ip.h"
#include "../../wifi/wifi.h" // Need access to s_wifi_event_group
#include "../../utils/ipify/ipify.h"
#include "../../utils/led/led_utils.h"
#include "../../utils/telegram/queue.h"

static const char *TAG = "PUBLIC_IP";

static bool initial_boot_done = false;

// Global Variables
char public_ip[64] = {0};
TaskHandle_t public_ip_task_handle = NULL;

// Public IP Config
static const uint32_t IP_CHECK_INTERVAL_MS = 20 * 60 * 1000; // 20 Mins
static const uint32_t IP_RETRY_DELAY_MS = 10 * 1000;         // 10 Secs (Fast retry on fail)

static bool check_public_ip(char *buffer, size_t buffer_len)
{
    // Try to get IP
    if (get_public_ip(buffer, buffer_len) == ESP_OK)
    {
        // Compare new IP with stored IP
        if (strcmp(public_ip, buffer) != 0)
        {
            ESP_LOGW(TAG, "Public IP changed from %s to %s", public_ip, buffer);

            // Update global variable safely
            memset(public_ip, 0, sizeof(public_ip));
            strncpy(public_ip, buffer, sizeof(public_ip) - 1);

            // First time success logic
            if (!initial_boot_done)
            {
                ESP_LOGI(TAG, "Initial Public IP acquired: %s", public_ip);
                post_message_to_queue("Esp32 online 🚀\nhttps://%s", true, public_ip);
                led_utils_set_blinks(0);
                initial_boot_done = true;
            }
            else
            {
                post_message_to_queue("Public IP change to:\nhttps://%s", true, public_ip);
            }
        }
        else
        {
            ESP_LOGI(TAG, "Public IP has not changed.");
        }
        return true;
    }

    ESP_LOGE(TAG, "IP check failed (Service down or timeout).");
    return false;
}

void public_ip_management_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting Public IP Management Task");

    // Register self so wifi.c can notify us
    public_ip_task_handle = xTaskGetCurrentTaskHandle();

    char temp_ip[64]; // Stack buffer for operations

    for (;;)
    {
        // Gate: Block indefinitely until WiFi is connected.
        //    If WiFi disconnects mid-operation, the next loop will block here.
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);

        bool success = check_public_ip(temp_ip, sizeof(temp_ip));

        if (success)
        {
            // Wait 20 mins OR until notified (WiFi Reconnected)
            //    ulTaskNotifyTake returns > 0 if notified, 0 if timed out.
            //    If we are notified (reconnect), we loop and check immediately.
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(IP_CHECK_INTERVAL_MS));
        }
        else
        {
            // Failure (e.g., ipify down). Retry quickly.
            // Use WaitBits with timeout to ensure WiFi disconnects are  respected during retry delay.
            // If WiFi drops during this 10s, we wake up immediately next loop and block at the Gate.
            xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                pdFALSE, pdTRUE, pdMS_TO_TICKS(IP_RETRY_DELAY_MS));
        }
    }
}