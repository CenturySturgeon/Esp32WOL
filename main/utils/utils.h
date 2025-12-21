#ifndef UTILS_H
#define UTILS_H

#include "esp_http_client.h"
#include <stdint.h>

esp_err_t get_public_ip(char *ip_buf, size_t ip_buf_len);
esp_err_t send_wol_packet(const uint8_t *mac_addr_hex);

#endif // UTILS_H