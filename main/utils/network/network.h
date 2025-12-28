#pragma once
#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include "esp_err.h"

typedef struct
{
    char alias[64];
    char ip[32];
    char ports[64]; // stored as "443|80|8080" for parsing later
} host_t;

esp_err_t network_set_host_list(host_t *list, uint8_t count);
esp_err_t network_ping_all_hosts(void);
esp_err_t send_wol_packet(const char *mac_str, const char *secure_str, const char *broadcast_str);
esp_err_t service_check(const char *ip_str, uint16_t port, int timeout_ms);

#endif // NETWORK_UTILS_H