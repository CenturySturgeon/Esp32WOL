#include "utils.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "../auth/auth.h"

static const char *TAG = "UTILS";

extern const char telegram_pem_start[] asm("_binary_telegram_pem_start");
extern const char telegram_pem_end[] asm("_binary_telegram_pem_end");

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

// Internal synchronous sender (The original function, made static)
static esp_err_t _telegram_send_message_sync(const char *message, bool silent)
{
    char token[64] = {0};
    char chat_id[20] = {0};
    char post_data[512] = {0};
    char url[128] = {0};

    // Fetch secrets
    if (auth_get_telegram_secrets(token, sizeof(token), chat_id, sizeof(chat_id)) != ESP_OK)
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
                esp_err_t res = _telegram_send_message_sync(msg_buffer.message, msg_buffer.silent);

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
void telegram_system_init(void)
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
bool telegram_post_to_queue(const char *fmt, bool silent, ...)
{
    if (telegram_queue == NULL)
    {
        ESP_LOGE(TAG, "Queue not initialized! Call telegram_system_init() first.");
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

// Decodes html urls (hmac's ":" are encoded to "%3A")
void url_decode(char *src)
{
    char *dst = src;
    while (*src)
    {
        if (*src == '%' && src[1] && src[2])
        {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

esp_err_t send_wol_packet(const char *mac_str, const char *secure_str, const char *broadcast_str)
{
    uint8_t mac_hex[6];
    uint8_t secure_hex[6];
    bool use_secure = (secure_str && strlen(secure_str) > 0);

    // Convert MAC string to hex
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac_hex[0], &mac_hex[1], &mac_hex[2], &mac_hex[3], &mac_hex[4], &mac_hex[5]) != 6)
    {
        ESP_LOGE(TAG, "Invalid MAC format");
        return ESP_FAIL;
    }

    // Convert SecureOn string to hex if provided
    if (use_secure)
    {
        sscanf(secure_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &secure_hex[0], &secure_hex[1], &secure_hex[2], &secure_hex[3], &secure_hex[4], &secure_hex[5]);
    }

    // Packet size: 102 (standard) + 6 (SecureOn) = 108
    uint8_t packet[108];
    size_t packet_size = use_secure ? 108 : 102;

    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++)
    {
        memcpy(&packet[6 + (i * 6)], mac_hex, 6);
    }

    if (use_secure)
    {
        memcpy(&packet[102], secure_hex, 6);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
        return ESP_FAIL;

    int broadcast_permission = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_permission, sizeof(broadcast_permission));

    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(9);

    // Use user IP if provided, otherwise fallback to global broadcast
    if (broadcast_str && strlen(broadcast_str) > 6)
    {
        dest_addr.sin_addr.s_addr = inet_addr(broadcast_str);
    }
    else
    {
        dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }

    int err = sendto(sock, packet, packet_size, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
        ESP_LOGE(TAG, "Sendto failed: %d", errno);

    close(sock);
    return (err < 0) ? ESP_FAIL : ESP_OK;
}
