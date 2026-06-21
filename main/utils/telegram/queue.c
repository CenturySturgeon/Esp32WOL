#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

// FreeRTOS Must stay at the top
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "queue.h"
#include "telegram.h"

#include "esp_log.h"

static const char *TAG = "NOTIFS_QUEUE";

// Notification Queue config
#define TG_QUEUE_DEPTH 5        // Max number of messages
#define TG_MSG_MAX_LEN 256      // Max message length
#define TG_TASK_STACK_SIZE 6144 // HTTPS needs significant stack
#define TG_TASK_PRIORITY 3      // Lower than Wi-Fi, higher than idle

// Queue backoff Configuration
#define RETRY_START_MS 5000 // 5 Seconds
#define RETRY_MAX_MS 120000 // 2 Minutes
#define TG_MAX_RETRIES 5    // Max retries per message before dropping

typedef struct
{
    char message[TG_MSG_MAX_LEN];
    bool silent;
} telegram_msg_t;

static QueueHandle_t telegram_queue = NULL;
static bool telegram_config_logged = false; // Track if we've logged the unconfigured warning

// Dedicated Sender Task
static void telegram_sender_task(void *pvParameters)
{
    telegram_msg_t msg_buffer;

    ESP_LOGI(TAG, "Notification Sender Task Started");

    for (;;)
    {
        if (xQueueReceive(telegram_queue, &msg_buffer, portMAX_DELAY) == pdTRUE)
        {
            if (!is_telegram_configured())
            {
                if (!telegram_config_logged)
                {
                    ESP_LOGI(TAG, "Telegram not configured. Notifications disabled.");
                    telegram_config_logged = true;
                }
                continue;
            }

            // Try to send with Exponential Backoff
            uint32_t current_delay_ms = RETRY_START_MS;
            int retries = 0;

            while (retries < TG_MAX_RETRIES)
            {
                esp_err_t res = send_telegram_message_sync(msg_buffer.message, msg_buffer.silent);

                if (res == ESP_OK)
                {
                    break; // Success! Exit retry loop
                }

                ESP_LOGW(TAG, "Send failed. Retry %d/%d in %lu ms...", retries + 1, TG_MAX_RETRIES, current_delay_ms);
                vTaskDelay(pdMS_TO_TICKS(current_delay_ms));

                // Exponential backoff logic
                current_delay_ms *= 2;
                if (current_delay_ms > RETRY_MAX_MS)
                {
                    current_delay_ms = RETRY_MAX_MS;
                }
                retries++;
            }

            if (retries >= TG_MAX_RETRIES)
            {
                ESP_LOGE(TAG, "Telegram send failed after %d retries. Dropping message.", TG_MAX_RETRIES);
            }
        }
    }
}

// Public initializer
void initialize_notifications_queue(void)
{
    if (telegram_queue != NULL)
    {
        ESP_LOGW(TAG, "Telegram queue already initialized");
        return;
    }

    telegram_queue = xQueueCreate(TG_QUEUE_DEPTH, sizeof(telegram_msg_t));
    if (telegram_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create Telegram Queue");
        return;
    }

    // Check task creation result & clean up on failure
    BaseType_t xRet = xTaskCreate(telegram_sender_task, "tg_sender", TG_TASK_STACK_SIZE, NULL, TG_TASK_PRIORITY, NULL);
    if (xRet != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create Telegram sender task");
        vQueueDelete(telegram_queue); // Prevent orphaned queue leak
        telegram_queue = NULL;
        return;
    }
}

// Public message sender
bool post_message_to_queue(const char *fmt, bool silent, ...)
{
    if (!is_telegram_configured())
    {
        return false;
    }

    if (telegram_queue == NULL)
    {
        ESP_LOGE(TAG, "Queue not initialized! Call initialize_notifications_queue() first.");
        return false;
    }

    telegram_msg_t new_msg;
    new_msg.silent = silent;

    va_list args;
    va_start(args, silent);
    vsnprintf(new_msg.message, TG_MSG_MAX_LEN, fmt, args);
    va_end(args);

    // Atomic drop to prevent race conditions across tasks
    if (xQueueSend(telegram_queue, &new_msg, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "Queue full! Dropping oldest message.");

        telegram_msg_t dummy;
        portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; // ESP-IDF v5 requires portMUX_TYPE instead of UBaseType_t
        taskENTER_CRITICAL(&mux);

        xQueueReceive(telegram_queue, &dummy, 0); // Guaranteed to free one slot
        BaseType_t send_res = xQueueSend(telegram_queue, &new_msg, 0);

        taskEXIT_CRITICAL(&mux);

        if (send_res != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to enqueue message even after drop.");
            return false;
        }
    }

    return true;
}
