#ifndef AUTH_H
#define AUTH_H

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h" // For session expiry

#include "nvs.h"
#include "nvs_flash.h"
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

esp_err_t nvs_init_and_load_secrets();
esp_err_t nvs_get_wifi_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t nvs_get_static_ip_config(esp_netif_ip_info_t *ip_info, bool *static_enabled);
esp_err_t nvs_get_telegram_secrets(char *token, size_t token_len, char *chat_id, size_t chat_id_len);

esp_err_t auth_login_user(const char *username, const char *password, char *out_token);
esp_err_t auth_check_session(const char *token);
esp_err_t auth_get_user_hmac_via_token(const char *token, uint8_t *hmac_out, size_t *hmac_len);
esp_err_t auth_check_totp_request(const char *token, const uint32_t pin);

#endif // AUTH_H