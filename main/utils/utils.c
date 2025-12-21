#include "utils.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "UTILS";

extern const char api_ipify_pem_start[] asm("_binary_api_ipify_pem_start");
extern const char api_ipify_pem_end[] asm("_binary_api_ipify_pem_end");

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

esp_err_t send_wol_packet(const uint8_t *mac_addr_hex)
{
    // Create the Magic Packet (6 * 0xFF + 16 * MAC)
    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++)
    {
        memcpy(&packet[6 + (i * 6)], mac_addr_hex, 6);
    }

    // Create UDP Socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Set Socket Options to allow Broadcast
    int broadcast_permission = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_permission, sizeof(broadcast_permission)) < 0)
    {
        ESP_LOGE(TAG, "Failed to set broadcast option: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }

    // 4. Set Destination Address (Standard WoL Port 9)
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST); // 255.255.255.255
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(9);

    // Send the packet
    int err = sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
    }
    else
    {
        ESP_LOGI(TAG, "Magic Packet sent successfully!");
    }

    close(sock);
    return (err < 0) ? ESP_FAIL : ESP_OK;
}
