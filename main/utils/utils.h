#ifndef UTILS_H
#define UTILS_H

#include "esp_http_client.h"
#include <stdint.h>
#include <stdbool.h>

// Initialize the Telegram Queue and Sender Task
void telegram_system_init(void);

// Non-blocking, thread-safe function to queue a message
// Usage: telegram_post_to_queue("Temp: %d", false, 25);
bool telegram_post_to_queue(const char *fmt, bool silent, ...);

// Existing utilities
esp_err_t get_public_ip(char *ip_buf, size_t ip_buf_len);
esp_err_t send_wol_packet(const char *mac_str, const char *secure_str, const char *broadcast_str);
void url_decode(char *src);

#endif // UTILS_H