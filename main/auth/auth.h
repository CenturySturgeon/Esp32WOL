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

esp_err_t auth_set_user_list(user_session_t *list, uint8_t count);
esp_err_t auth_logout_user(const char *token);
esp_err_t auth_logout_all_users();
esp_err_t auth_login_user(const char *username, const char *password, char *out_token, uint8_t *ttl_out);
esp_err_t auth_check_session(const char *token);
esp_err_t auth_check_totp_request(const char *token, const uint32_t pin);
esp_err_t auth_semaphore_init();

#endif // AUTH_H