#include <stdio.h>
#include <string.h>

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "ping/ping_sock.h"

#include "network.h"

static host_t *hosts_list = NULL;
static uint8_t total_hosts_count = 0;

static const char *TAG = "NETWORK UTILS";

// Callback for when a ping response is received or times out
static void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));

    ESP_LOGD(TAG, "%d bytes icmp_seq=%d ttl=%d time=%d ms", recv_len, seqno, ttl, elapsed_time);
}

static void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    ESP_LOGD(TAG, "From ping: timeout icmp_seq=%d", seqno);
}

static void cmd_ping_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    ESP_LOGI(TAG, "Ping session finished. Cleaning up resources...");

    // Delete session, free memory
    esp_ping_delete_session(hdl);
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

esp_err_t send_ping(const char *ip_str)
{
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();

    ip_addr_t target_addr;
    memset(&target_addr, 0, sizeof(target_addr));

    // Parse the IP string
    if (ipaddr_aton(ip_str, &target_addr) == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ping_config.target_addr = target_addr;
    ping_config.count = 5; // 0 means infinite ping
    esp_ping_callbacks_t cbs = {
        .on_ping_success = cmd_ping_on_ping_success,
        .on_ping_timeout = cmd_ping_on_ping_timeout,
        .on_ping_end = cmd_ping_on_ping_end,
        .cb_args = NULL};

    esp_ping_handle_t ping_handle;
    esp_err_t err = esp_ping_new_session(&ping_config, &cbs, &ping_handle);

    if (err == ESP_OK)
    {
        err = esp_ping_start(ping_handle);
        if (err != ESP_OK)
        {
            esp_ping_delete_session(ping_handle);
            return err;
        }

        return ESP_OK;
    }

    return ESP_FAIL;
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