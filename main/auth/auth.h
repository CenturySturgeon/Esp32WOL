#pragma once
#ifndef AUTH_H
#define AUTH_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"

#include "nvs.h"
#include <stdio.h>
#include <string.h>

// Global pointers to hold our users once loaded
typedef struct
{
    char name[32];
    uint8_t ttl;            // Session TTL in seconds
    char hash[65];          // Hex string for SHA256 (32 bytes * 2 + null)
    uint8_t hmac[10];       // Binary TOTP key
    char session_token[33]; // 32 chars + null
    int64_t session_expiry; // Timestamp in microseconds
} user_session_t;

extern user_session_t *users_list;

esp_err_t auth_login_user(const char *username, const char *password, char *out_token);
esp_err_t auth_check_session(const char *token);
esp_err_t auth_get_user_hmac_via_token(const char *token, uint8_t *hmac_out, size_t *hmac_len);
esp_err_t auth_check_totp_request(const char *token, const uint32_t pin);
esp_err_t auth_semaphore_init();

#endif // AUTH_H