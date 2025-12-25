#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"

#include "network.h"

static const char *TAG = "NETWORK UTILS";

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