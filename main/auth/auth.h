#ifndef AUTH_H
#define AUTH_H

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

// Global pointers to hold our users once loaded
typedef struct
{
    char name[32];
    uint8_t ttl;
    char hash[65];    // Hex string for SHA256
    uint8_t hmac[10]; // Binary TOTP key
} user_session_t;

esp_err_t init_and_load_secrets();
esp_err_t auth_get_wifi_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t auth_get_user_hmac(const char *username, uint8_t *hmac_out, size_t *hmac_len);

#endif // AUTH_H