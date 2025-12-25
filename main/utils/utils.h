#ifndef UTILS_H
#define UTILS_H

#include "esp_err.h"

// Existing utilities
esp_err_t send_wol_packet(const char *mac_str, const char *secure_str, const char *broadcast_str);
void url_decode(char *src);

#endif // UTILS_H