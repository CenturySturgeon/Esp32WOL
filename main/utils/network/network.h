#pragma once
#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include "esp_err.h"

esp_err_t send_ping(const char *ip_str);
esp_err_t send_wol_packet(const char *mac_str, const char *secure_str, const char *broadcast_str);
esp_err_t service_check(const char *ip_str, uint16_t port, int timeout_ms);

#endif // NETWORK_UTILS_H