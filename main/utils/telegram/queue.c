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

typedef struct
{
    char message[TG_MSG_MAX_LEN];
    bool silent;
} telegram_msg_t;

static QueueHandle_t telegram_queue = NULL;

// Dedicated Sender Task
static void telegram_sender_task(void *pvParameters)
{
    telegram_msg_t msg_buffer;

    ESP_LOGI(TAG, "Notification Sender Task Started");

    for (;;)
    {
        // Waits indefinitely for a message to arrive
        if (xQueueReceive(telegram_queue, &msg_buffer, portMAX_DELAY) == pdTRUE)
        {
            // Try to send with Exponential Backoff
            uint32_t current_delay_ms = RETRY_START_MS;

            while (1)
            {
                esp_err_t res = send_telegram_message_sync(msg_buffer.message, msg_buffer.silent);

                if (res == ESP_OK)
                {
                    // Success! Break retry loop and wait for next message
                    break;
                }
                else
                {
                    ESP_LOGW(TAG, "Send failed. Retrying in %lu ms...", current_delay_ms);

                    // Non-blocking delay (yields to other tasks)
                    vTaskDelay(pdMS_TO_TICKS(current_delay_ms));

                    // Exponential backoff logic
                    current_delay_ms *= 2;
                    if (current_delay_ms > RETRY_MAX_MS)
                    {
                        current_delay_ms = RETRY_MAX_MS;
                    }
                }
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

    xTaskCreate(telegram_sender_task, "tg_sender", TG_TASK_STACK_SIZE, NULL, TG_TASK_PRIORITY, NULL);
}

// Public message sender
bool post_message_to_queue(const char *fmt, bool silent, ...)
{
    if (telegram_queue == NULL)
    {
        ESP_LOGE(TAG, "Queue not initialized! Call initialize_notifications_queue() first.");
        return false;
    }

    telegram_msg_t new_msg;
    new_msg.silent = silent;

    // Format the string safely
    va_list args;
    va_start(args, silent);
    vsnprintf(new_msg.message, TG_MSG_MAX_LEN, fmt, args);
    va_end(args);

    // Try to send to queue
    if (xQueueSend(telegram_queue, &new_msg, 0) != pdTRUE)
    {
        // QUEUE IS FULL: Drop oldest message to make space for the new one
        ESP_LOGW(TAG, "Queue full! Dropping oldest message.");

        telegram_msg_t dummy;
        xQueueReceive(telegram_queue, &dummy, 0); // Drop oldest

        // Try enqueuing again
        if (xQueueSend(telegram_queue, &new_msg, 0) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to enqueue message even after drop.");
            return false;
        }
    }

    return true;
}
