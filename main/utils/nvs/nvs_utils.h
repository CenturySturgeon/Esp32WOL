#pragma once
#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif_types.h"

esp_err_t nvs_init_and_load_secrets();
esp_err_t nvs_get_wifi_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t nvs_get_static_ip_config(esp_netif_ip_info_t *ip_info, bool *static_enabled);
esp_err_t nvs_get_telegram_secrets(char *token, size_t token_len, char *chat_id, size_t chat_id_len);
esp_err_t nvs_get_duckdns_secrets(char *token, size_t token_len, char *domain, size_t domain_len);


// Certificate NVS functions
esp_err_t nvs_load_certs(uint8_t **cert_buf, size_t *cert_len, uint8_t **key_buf, size_t *key_len);
esp_err_t nvs_save_certs(const uint8_t *cert_data, size_t cert_size, const uint8_t *key_data, size_t key_size);
bool nvs_has_nvs_certs(void);

// Certificate update key
esp_err_t nvs_get_cert_update_key(char *key, size_t key_len);
esp_err_t nvs_set_cert_update_key(const char *key, size_t key_len);
int nvs_get_custom_port(void);

#endif // NVS_UTILS_H