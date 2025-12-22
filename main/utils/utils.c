#include "utils.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "lwip/sockets.h"
#include <string.h>

#include "../auth/auth.h"

static const char *TAG = "UTILS";

extern const char api_ipify_pem_start[] asm("_binary_api_ipify_pem_start");
extern const char api_ipify_pem_end[] asm("_binary_api_ipify_pem_end");
extern const char telegram_pem_start[] asm("_binary_telegram_pem_start");
extern const char telegram_pem_end[] asm("_binary_telegram_pem_end");

static esp_err_t _ipify_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_CONNECTED:
        if (evt->user_data)
        {
            memset(evt->user_data, 0, 64);
        }
        break;

    case HTTP_EVENT_ON_DATA:
        if (evt->user_data)
        {
            char *buf = (char *)evt->user_data;
            int current_len = strlen(buf);

            // Check if there is space for at least some of the new data + null terminator
            if (current_len < 63)
            {
                int remaining_space = 63 - current_len;
                int copy_len = (evt->data_len < remaining_space) ? evt->data_len : remaining_space;

                memcpy(buf + current_len, evt->data, copy_len);
                buf[current_len + copy_len] = '\0';
            }
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;

    default:
        break;
    }
    return ESP_OK;
}

esp_err_t get_public_ip(char *ip_buf, size_t ip_buf_len)
{
    if (!ip_buf || ip_buf_len < 16)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize buffer with empty string
    ip_buf[0] = '\0';

    ESP_LOGI(TAG, "Requesting Public IP from api.ipify.org...");

    esp_http_client_config_t config = {
        .url = "https://api.ipify.org",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = api_ipify_pem_start,
        .event_handler = _ipify_event_handler,
        .user_data = ip_buf,
        .timeout_ms = 8000, // Slightly increased for reliability over SSL
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code != 200)
        {
            ESP_LOGW(TAG, "Unexpected HTTP status: %d", status_code);
            err = ESP_FAIL;
        }
        // Verify we actually got data in the buffer
        if (strlen(ip_buf) == 0)
        {
            ESP_LOGE(TAG, "HTTP OK but IP buffer is empty");
            err = ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
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

esp_err_t telegram_send_message(const char *message, bool silent)
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
    snprintf(post_data, sizeof(post_data),
             "{\"chat_id\":\"%s\",\"text\":\"%s\",\"disable_notification\":%s}",
             chat_id,
             message,
             silent ? "true" : "false");

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .cert_pem = telegram_pem_start,
        .timeout_ms = 10000, // Increased timeout for TLS handshake
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP POST Status = %d", esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    memset(token, 0, sizeof(token));
    memset(chat_id, 0, sizeof(chat_id));

    return err;
}
