#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "nvs_utils.h"
#include "../../auth/auth.h"

static bool nvs_already_initialized = false;

static const char *TAG = "NVS_UTILS";

esp_err_t nvs_init_and_load_secrets()
{

    if (nvs_already_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init_partition("storage");
    if (err != ESP_OK)
        return err;

    nvs_handle_t handle;
    err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    uint8_t count = 0;
    nvs_get_u8(handle, "total_users", &count);

    user_session_t *temp_list = NULL;

    if (count > 0)
    {
        temp_list = malloc(sizeof(user_session_t) * count);
        if (temp_list == NULL)
        {
            nvs_close(handle);
            return ESP_ERR_NO_MEM;
        }

        for (int i = 1; i <= count; i++)
            for (int i = 1; i <= count; i++)
            {
                char key[32];
                size_t required_size;

                // Load Name - Note the change to temp_list
                snprintf(key, sizeof(key), "user_%d_name", i);
                required_size = sizeof(temp_list[i - 1].name);                   // Fixed
                nvs_get_str(handle, key, temp_list[i - 1].name, &required_size); // Fixed

                // Load TTL
                snprintf(key, sizeof(key), "user_%d_TTL", i);
                nvs_get_u8(handle, key, &temp_list[i - 1].ttl); // Fixed

                // Load Hash
                snprintf(key, sizeof(key), "user_%d_hash", i);
                required_size = sizeof(temp_list[i - 1].hash);                   // Fixed
                nvs_get_str(handle, key, temp_list[i - 1].hash, &required_size); // Fixed

                // Load HMAC Binary Blob
                snprintf(key, sizeof(key), "user_%d_hmac", i);
                required_size = sizeof(temp_list[i - 1].hmac);                    // Fixed
                nvs_get_blob(handle, key, temp_list[i - 1].hmac, &required_size); // Fixed
            }
    }

    // Hand the data over to auth.c
    if (auth_set_user_list(temp_list, count) != ESP_OK)
    {
        free(temp_list); // Cleanup if the hand-off fails
        nvs_close(handle);
        return ESP_FAIL;
    }

    if (auth_semaphore_init() != ESP_OK)
    {
        nvs_close(handle);
        return ESP_FAIL;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_get_wifi_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t handle;
    // Open the custom 'storage' partition
    esp_err_t err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    // Fetch SSID and Password
    err = nvs_get_str(handle, "wifi_ssid", ssid, &ssid_len);
    if (err == ESP_OK)
    {
        err = nvs_get_str(handle, "wifi_pass", pass, &pass_len);
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_get_static_ip_config(esp_netif_ip_info_t *ip_info, bool *static_enabled)
{
    if (!ip_info || !static_enabled)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(
        "storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    uint8_t enabled = 0;
    err = nvs_get_u8(handle, "use_static_ip", &enabled);
    if (err != ESP_OK || enabled == 0)
    {
        *static_enabled = false;
        nvs_close(handle);
        return ESP_OK;
    }

    char ip[16], gw[16], mask[16];
    size_t len;

    len = sizeof(ip);
    err = nvs_get_str(handle, "static_ip", ip, &len);
    if (err != ESP_OK)
        goto fail;

    len = sizeof(gw);
    err = nvs_get_str(handle, "router_gw", gw, &len);
    if (err != ESP_OK)
        goto fail;

    len = sizeof(mask);
    err = nvs_get_str(handle, "router_mask", mask, &len);
    if (err != ESP_OK)
        goto fail;

    esp_netif_str_to_ip4(ip, &ip_info->ip);
    esp_netif_str_to_ip4(gw, &ip_info->gw);
    esp_netif_str_to_ip4(mask, &ip_info->netmask);

    *static_enabled = true;
    nvs_close(handle);
    return ESP_OK;

fail:
    nvs_close(handle);
    return err;
}

esp_err_t nvs_get_telegram_secrets(char *token, size_t token_len, char *chat_id, size_t chat_id_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_get_str(handle, "bot_token", token, &token_len);
    if (err == ESP_OK)
    {
        err = nvs_get_str(handle, "chat_id", chat_id, &chat_id_len);
    }

    nvs_close(handle);
    return err;
}
