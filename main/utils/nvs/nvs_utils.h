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

#endif // NVS_UTILS_H