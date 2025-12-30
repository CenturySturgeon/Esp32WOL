#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
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

        post_message_to_queue(final_report, true);

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

static void run_services_scan_task(void *pvParameters)
{
    // Allocate a large buffer for the report (on heap to save stack)
    // Adjust size based on max hosts * max ports
    char *report_buffer = calloc(1, 1024);
    if (!report_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate report buffer");
        vTaskDelete(NULL);
        return;
    }

    // Header
    strcat(report_buffer, "⚙️ *Service Status* ⚙️\n");

    for (int i = 0; i < total_hosts_count; i++)
    {
        host_t *host = &hosts_list[i];

        // ESP_LOGI(TAG, "Host %d", i);
        // ESP_LOGI(TAG, "  Alias      : '%s'", host->alias);
        // ESP_LOGI(TAG, "  IP         : '%s'", host->ip);
        // ESP_LOGI(TAG, "  Ports      : '%s'", host->ports);
        // ESP_LOGI(TAG, "  Port names : '%s'", host->port_names);

        // Skip if no ports defined
        if (strlen(host->ports) == 0)
            continue;

        // Work on a copy of the strings because strtok modifies them
        char ports_copy[64];
        char names_copy[256];
        strncpy(ports_copy, host->ports, sizeof(ports_copy) - 1);
        strncpy(names_copy, host->port_names, sizeof(names_copy) - 1);
        ports_copy[sizeof(ports_copy) - 1] = '\0';
        names_copy[sizeof(names_copy) - 1] = '\0';

        // ESP_LOGI(TAG, "  ports_copy : '%s'", ports_copy);
        // ESP_LOGI(TAG, "  names_copy : '%s'", names_copy);

        int total_services = 0;
        char host_line[384] = {0};
        char details[256] = {0};

        // Tokenize both strings in parallel
        char *port_saveptr;
        char *name_saveptr;

        char *port_token = strtok_r(ports_copy, "|", &port_saveptr);
        char *name_token = strtok_r(names_copy, "|", &name_saveptr);

        while (port_token != NULL && name_token != NULL)
        {
            // ESP_LOGI(TAG, "  Token pair: ='%s' name='%s'",
            //          port_token, name_token);

            int port = atoi(port_token);
            if (port > 0)
            {
                total_services++;
                esp_err_t res = service_check(host->ip, port, 2000);

                char port_res[64];
                snprintf(port_res, sizeof(port_res), "%s %s",
                         name_token,
                         (res == ESP_OK) ? "⬆️ " : "⬇️ ");

                if (strlen(details) + strlen(port_res) < sizeof(details))
                {
                    strcat(details, port_res);
                }
            }

            port_token = strtok_r(NULL, "|", &port_saveptr);
            name_token = strtok_r(NULL, "|", &name_saveptr);
        }

        if (total_services > 0)
        {
            snprintf(host_line, sizeof(host_line), "\n*%s*\n%s \n", host->alias, details);

            if (strlen(report_buffer) + strlen(host_line) < 1023)
            {

                strcat(report_buffer, host_line);
            }
        }
    }

    ESP_LOGI(TAG, "Service scan complete. Sending message to queue...");

    post_message_to_queue(report_buffer, true);

    free(report_buffer);
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
    ESP_LOGI(TAG, "Pinging all hosts...");
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
    struct sockaddr_in dest_addr;
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
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0 && errno != EINPROGRESS)
    {
        close(sock);
        return ESP_FAIL;
    }

    // Wait for the socket to be writable (connected)
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int select_res = select(sock + 1, NULL, &fdset, NULL, &tv);

    if (select_res > 0)
    {
        // Check for socket errors to confirm actual connection
        int so_error;
        socklen_t len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);

        close(sock);
        return (so_error == 0) ? ESP_OK : ESP_FAIL;
    }
    else
    {
        // Timeout or error
        close(sock);
        return ESP_FAIL;
    }
}

esp_err_t network_scan_services(void)
{
    ESP_LOGI(TAG, "Starting services scan...");
    if (hosts_list == NULL || total_hosts_count == 0)
        return ESP_ERR_INVALID_STATE;

    // Stack depth 4096 is usually enough for socket ops + formatting
    xTaskCreate(run_services_scan_task, "srv_scan_task", 4096, NULL, 5, NULL);

    return ESP_OK;
}