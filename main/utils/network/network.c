#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "ping/ping_sock.h"

#include "network.h"
#include "../telegram/queue.h"

// Shared state for the batch ping operation
static struct
{
    uint8_t current_index;
    char success_buffer[512]; // Sum of all host's aliases expectation
    SemaphoreHandle_t lock;
} ping_summary_state;

static host_t *hosts_list = NULL;
static uint8_t total_hosts_count = 0;

static const char *TAG = "NETWORK UTILS";

static void batch_ping_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t received_count = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received_count, sizeof(received_count));

    if (received_count > 0)
    {
        host_t *current_host = &hosts_list[ping_summary_state.current_index];

        // Append alias to the buffer (with safety check)
        size_t current_len = strlen(ping_summary_state.success_buffer);
        size_t alias_len = strlen(current_host->alias);

        if (current_len + alias_len + 3 < sizeof(ping_summary_state.success_buffer))
        {
            strcat(ping_summary_state.success_buffer, current_host->alias);
            strcat(ping_summary_state.success_buffer, ", ");
        }
    }

    esp_ping_delete_session(hdl);

    // Unblock the task to process the next host
    xSemaphoreGive(ping_summary_state.lock);
}

static void check_all_hosts_task(void *pvParameters)
{
    ping_summary_state.success_buffer[0] = '\0';

    for (int i = 0; i < total_hosts_count; i++)
    {
        ping_summary_state.current_index = i;

        esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
        ip_addr_t target_addr;

        if (ipaddr_aton(hosts_list[i].ip, &target_addr))
        {
            ping_config.target_addr = target_addr;
            ping_config.count = 2; // Fast check: 2 attempts

            esp_ping_callbacks_t cbs = {
                .on_ping_end = batch_ping_on_ping_end,
                .cb_args = NULL};

            esp_ping_handle_t ping_handle;
            if (esp_ping_new_session(&ping_config, &cbs, &ping_handle) == ESP_OK)
            {
                esp_ping_start(ping_handle);
                // Wait for the callback to finish this host
                xSemaphoreTake(ping_summary_state.lock, portMAX_DELAY);
            }
        }
    }

    // Final Message Construction
    if (strlen(ping_summary_state.success_buffer) > 0)
    {
        // Trim trailing comma and space
        ping_summary_state.success_buffer[strlen(ping_summary_state.success_buffer) - 2] = '\0';

        char final_report[600];
        snprintf(final_report, sizeof(final_report), "🌐 Online hosts 🌐\n[%s]", ping_summary_state.success_buffer);

        post_message_to_queue(final_report, false);

        ESP_LOGI(TAG, "At least one host is online...");
    }
    else
    {
        post_message_to_queue("🖥️❌\nAll hosts unreachable", false);
        ESP_LOGW(TAG, "Network Status: All hosts unreachable.");
    }

    vSemaphoreDelete(ping_summary_state.lock);
    ping_summary_state.lock = NULL;
    vTaskDelete(NULL);
}

esp_err_t network_set_host_list(host_t *list, uint8_t count)
{
    ESP_LOGI(TAG, "Initializing hosts...");

    if (hosts_list != NULL)
    {
        // Already initialized, prevent re-init
        return ESP_ERR_INVALID_STATE;
    }

    if (list == NULL && count > 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    hosts_list = list;
    total_hosts_count = count;
    return ESP_OK;
}

esp_err_t network_ping_all_hosts(void)
{
    ESP_LOGI(TAG, "Pinging al hosts...")
    if (hosts_list == NULL || total_hosts_count == 0)
        return ESP_ERR_INVALID_STATE;

    ping_summary_state.lock = xSemaphoreCreateBinary();

    // Run in a low priority task (5) to not interfere with the Web Server
    xTaskCreate(check_all_hosts_task, "check_all_hosts_task", 4096, NULL, 5, NULL);

    return ESP_OK;
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

    struct sockaddr_in dest_addr = {0};
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

esp_err_t service_check(const char *ip_str, uint16_t port, int timeout_ms)
{
    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_addr.s_addr = inet_addr(ip_str);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Set socket to Non-Blocking mode
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
    {
        close(sock);
        return ESP_FAIL;
    }
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    ESP_LOGI(TAG, "Checking service (Non-blocking)...");

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    // If connect returns 0 immediately, it connected instantly (rare on remote, possible on localhost)
    if (err == 0)
    {
        ESP_LOGI(TAG, "Connected immediately!");
        close(sock);
        return ESP_OK;
    }

    // If err is -1, we expect errno to be EINPROGRESS (connection started but not finished)
    if (errno != EINPROGRESS)
    {
        ESP_LOGW(TAG, "Connection failed immediately: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }

    // Use select() to wait for the socket to become writable (connected) or timeout
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    // select(max_fd + 1, read_set, write_set, error_set, timeout)
    // Listen to the write_set because a socket becomes 'writable' when connected
    int res = select(sock + 1, NULL, &fdset, NULL, &tv);

    if (res < 0)
    {
        ESP_LOGE(TAG, "Select error: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }
    else if (res == 0)
    {
        ESP_LOGW(TAG, "Connection timed out after %d ms", timeout_ms);
        close(sock);
        return ESP_FAIL;
    }
    else
    {
        // Even if select returns > 0, the connection might have failed (e.g. refused)
        int sock_err = 0;
        socklen_t len = sizeof(sock_err);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &sock_err, &len) < 0)
        {
            ESP_LOGE(TAG, "getsockopt failed");
            close(sock);
            return ESP_FAIL;
        }

        if (sock_err != 0)
        {
            ESP_LOGW(TAG, "Connection failed after select: error %d", sock_err);
            close(sock);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Service is available!");
        close(sock);
        return ESP_OK;
    }
}
