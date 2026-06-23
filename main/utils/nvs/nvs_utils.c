#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include <stdlib.h>
#include "nvs_utils.h"
#include "../../auth/auth.h"
#include "../network/network.h"

static bool nvs_already_initialized = false;

static const char *TAG = "NVS_UTILS";

static esp_err_t nvs_load_hosts()
{
    ESP_LOGI(TAG, "Loading hosts from NVS...");

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    uint8_t count = 0;
    nvs_get_u8(handle, "total_hosts", &count);

    host_t *temp_list = NULL;

    if (count > 0)
    {
        temp_list = malloc(sizeof(host_t) * count);
        if (!temp_list)
        {
            nvs_close(handle);
            return ESP_ERR_NO_MEM;
        }

        for (uint8_t i = 0; i < count; i++)
        {
            char key[32];
            size_t required_size;

            snprintf(key, sizeof(key), "alias_h_%d", i);
            required_size = sizeof(temp_list[i].alias);
            nvs_get_str(handle, key, temp_list[i].alias, &required_size);

            snprintf(key, sizeof(key), "ip_h_%d", i);
            required_size = sizeof(temp_list[i].ip);
            nvs_get_str(handle, key, temp_list[i].ip, &required_size);

            // Load ports (can be empty)
            snprintf(key, sizeof(key), "ports_h_%d", i);
            required_size = sizeof(temp_list[i].ports);
            nvs_get_str(handle, key, temp_list[i].ports, &required_size);

            // Load port names (can be empty)
            snprintf(key, sizeof(key), "port_names_h_%d", i);
            required_size = sizeof(temp_list[i].port_names);
            nvs_get_str(handle, key, temp_list[i].port_names, &required_size);
        }
    }

    // Hand it off to network.c
    if (network_set_host_list(temp_list, count) != ESP_OK)
    {
        free(temp_list);
        nvs_close(handle);
        return ESP_FAIL;
    }

    nvs_close(handle);
    return ESP_OK;
}

static esp_err_t nvs_load_sessions()
{
    ESP_LOGI(TAG, "Loading user sessions from NVS...");

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
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
        {
            char key[32];
            size_t required_size;

            // Load Name - Note the change to temp_list
            snprintf(key, sizeof(key), "user_%d_name", i);
            required_size = sizeof(temp_list[i - 1].name);
            nvs_get_str(handle, key, temp_list[i - 1].name, &required_size);

            // Load TTL
            snprintf(key, sizeof(key), "user_%d_TTL", i);
            nvs_get_u8(handle, key, &temp_list[i - 1].ttl);

            // Load Salt (PBKDF2 salt)
            snprintf(key, sizeof(key), "user_%d_salt", i);
            required_size = sizeof(temp_list[i - 1].salt);                   
            nvs_get_str(handle, key, temp_list[i - 1].salt, &required_size);

            // Load Hash (PBKDF2-HMAC-SHA256)
            snprintf(key, sizeof(key), "user_%d_hash", i);
            required_size = sizeof(temp_list[i - 1].hash); 
            nvs_get_str(handle, key, temp_list[i - 1].hash, &required_size);

            // Load HMAC Binary Blob
            snprintf(key, sizeof(key), "user_%d_hmac", i);
            required_size = sizeof(temp_list[i - 1].hmac);
            nvs_get_blob(handle, key, temp_list[i - 1].hmac, &required_size);
        }
    }

    // Hand the data over to auth.c
    if (auth_set_user_list(temp_list, count) != ESP_OK)
    {
        free(temp_list); // Cleanup if the hand-off fails
        nvs_close(handle);
        return ESP_FAIL;
    }
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_init_and_load_secrets()
{
    ESP_LOGI(TAG, "Initializing NVS...");

    if (nvs_already_initialized)
    {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init_partition("storage");
    if (err != ESP_OK)
        return err;

    nvs_handle_t handle;
    err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    // Hand session data over to auth.c
    nvs_load_sessions();

    // Hand host data over to network.c
    nvs_load_hosts();

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
    ESP_LOGI(TAG, "Getting WIFI credentials...");
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
    ESP_LOGI(TAG, "Setting static IP config...");
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
    ESP_LOGI(TAG, "Getting telegram secrets...");
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

esp_err_t nvs_get_duckdns_secrets(char *token, size_t token_len, char *domain, size_t domain_len)
{
    ESP_LOGI(TAG, "Getting DuckDNS secrets...");
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_get_str(handle, "duckdns_token", token, &token_len);
    if (err == ESP_OK)
    {
        err = nvs_get_str(handle, "duckdns_domain", domain, &domain_len);
    }

    nvs_close(handle);
    return err;
}

int nvs_get_custom_port(void)
{
    char port_str[8] = {0};
    size_t len = sizeof(port_str);

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", "storage", NVS_READONLY, &handle);
    if (err != ESP_OK)
        return 443;

    err = nvs_get_str(handle, "custom_port", port_str, &len);
    nvs_close(handle);

    // Default to 443 if missing or invalid
    if (err != ESP_OK || strlen(port_str) == 0)
    {
        return 443;
    }

    int port = atoi(port_str);
    return (port > 0 && port <= 65535) ? port : 443; // Validate range
}

// Certificate NVS functions
static const char *CERT_NAMESPACE = "certs";
static const char *CERT_MAIN_KEY = "cert_main";
static const char *KEY_MAIN_KEY = "key_main";
static const char *CERT_BACKUP_KEY = "cert_backup";
static const char *KEY_BACKUP_KEY = "key_backup";

bool nvs_has_nvs_certs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", CERT_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return false;

    size_t len = 0;
    bool has_cert = (nvs_get_blob(handle, CERT_MAIN_KEY, NULL, &len) == ESP_OK && len > 0);

    nvs_close(handle);
    return has_cert;
}

esp_err_t nvs_load_certs(uint8_t **cert_buf, size_t *cert_len, uint8_t **key_buf, size_t *key_len)
{
    ESP_LOGI(TAG, "Loading certificates from NVS...");

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", CERT_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open certs namespace: %s", esp_err_to_name(err));
        return err;
    }

    // Load certificate
    size_t cert_size = 0;
    err = nvs_get_blob(handle, CERT_MAIN_KEY, NULL, &cert_size);
    if (err != ESP_OK || cert_size == 0)
    {
        ESP_LOGW(TAG, "No certificate found in NVS");
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    *cert_buf = malloc(cert_size);
    if (!*cert_buf)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for cert");
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, CERT_MAIN_KEY, *cert_buf, &cert_size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read certificate: %s", esp_err_to_name(err));
        free(*cert_buf);
        *cert_buf = NULL;
        nvs_close(handle);
        return err;
    }
    *cert_len = cert_size;

    // Load private key
    size_t key_size = 0;
    err = nvs_get_blob(handle, KEY_MAIN_KEY, NULL, &key_size);
    if (err != ESP_OK || key_size == 0)
    {
        ESP_LOGW(TAG, "No private key found in NVS");
        free(*cert_buf);
        *cert_buf = NULL;
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    *key_buf = malloc(key_size);
    if (!*key_buf)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for key");
        free(*cert_buf);
        *cert_buf = NULL;
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, KEY_MAIN_KEY, *key_buf, &key_size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read private key: %s", esp_err_to_name(err));
        free(*cert_buf);
        free(*key_buf);
        *cert_buf = NULL;
        *key_buf = NULL;
        nvs_close(handle);
        return err;
    }
    *key_len = key_size;

    nvs_close(handle);
    ESP_LOGI(TAG, "Certificates loaded successfully (cert: %zu bytes, key: %zu bytes)", *cert_len, *key_len);
    return ESP_OK;
}

static esp_err_t validate_cert_key_pair(const uint8_t *cert_der, size_t cert_len, const uint8_t *key_der, size_t key_len)
{
    mbedtls_x509_crt crt;
    mbedtls_pk_context pk;

    mbedtls_x509_crt_init(&crt);
    mbedtls_pk_init(&pk);

    esp_err_t ret = ESP_FAIL;

    int err = mbedtls_x509_crt_parse(&crt, cert_der, cert_len);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Certificate parsing failed: -0x%04x", -err);
        goto cleanup;
    }

    err = mbedtls_pk_parse_key(&pk, key_der, key_len, NULL, 0, NULL, NULL);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Private key parsing failed: -0x%04x", -err);
        goto cleanup;
    }

    // Verify that the certificate and key match
    // err = mbedtls_x509_crt_verify(&crt, &pk, NULL, NULL, NULL, 0, NULL, NULL);
    if (err != 0)
    {
        ESP_LOGI(TAG, "Certificate and key parsed successfully");
        // Don't fail on this, some self signed certs may have issues
    }

    ret = ESP_OK;

cleanup:
    mbedtls_x509_crt_free(&crt);
    mbedtls_pk_free(&pk);

    return ret;
}

esp_err_t nvs_save_certs(const uint8_t *cert_data, size_t cert_size, const uint8_t *key_data, size_t key_size)
{
    ESP_LOGI(TAG, "Saving certificates to NVS...");

    // Validate the cert/key pair before saving
    if (validate_cert_key_pair(cert_data, cert_size, key_data, key_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Certificate/key validation failed. Not saving.");
        return ESP_FAIL;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", CERT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open certs namespace: %s", esp_err_to_name(err));
        return err;
    }

    // Write to backup keys first
    ESP_LOGI(TAG, "Writing to backup keys...");
    err = nvs_set_blob(handle, CERT_BACKUP_KEY, cert_data, cert_size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write backup cert: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = nvs_set_blob(handle, KEY_BACKUP_KEY, key_data, key_size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write backup key: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to commit backup: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // Verify backup by reading it back
    size_t read_cert_size = cert_size;
    uint8_t *verify_buf = malloc(cert_size);
    if (!verify_buf)
    {
        ESP_LOGE(TAG, "Failed to allocate verify buffer");
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    size_t read_key_size = key_size;
    uint8_t *verify_key_buf = malloc(key_size);
    if (!verify_key_buf)
    {
        ESP_LOGE(TAG, "Failed to allocate verify key buffer");
        free(verify_buf);
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    err = nvs_get_blob(handle, CERT_BACKUP_KEY, verify_buf, &read_cert_size);
    if (err != ESP_OK || read_cert_size != cert_size)
    {
        ESP_LOGE(TAG, "Backup verification failed for cert");
        free(verify_buf);
        free(verify_key_buf);
        goto cleanup;
    }

    err = nvs_get_blob(handle, KEY_BACKUP_KEY, verify_key_buf, &read_key_size);
    if (err != ESP_OK || read_key_size != key_size)
    {
        ESP_LOGE(TAG, "Backup verification failed for key");
        free(verify_buf);
        free(verify_key_buf);
        goto cleanup;
    }

    // Verify data matches
    if (memcmp(cert_data, verify_buf, cert_size) != 0 ||
        memcmp(key_data, verify_key_buf, key_size) != 0)
    {
        ESP_LOGE(TAG, "Backup data mismatch after write");
        free(verify_buf);
        free(verify_key_buf);
        err = ESP_FAIL;
        goto cleanup;
    }

    free(verify_buf);
    free(verify_key_buf);

    // Swap to main keys
    ESP_LOGI(TAG, "Swapping backup to main keys...");
    err = nvs_set_blob(handle, CERT_MAIN_KEY, cert_data, cert_size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write main cert: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = nvs_set_blob(handle, KEY_MAIN_KEY, key_data, key_size);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write main key: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to commit main keys: %s", esp_err_to_name(err));
        goto cleanup;
    }

    ESP_LOGI(TAG, "Certificates saved successfully to NVS");

cleanup:
    nvs_close(handle);
    return err;
}

esp_err_t nvs_get_cert_update_key(char *key, size_t key_len)
{
    ESP_LOGI(TAG, "Getting cert update key...");
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", CERT_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_get_str(handle, "cert_update_key", key, &key_len);

    nvs_close(handle);
    return err;
}

esp_err_t nvs_set_cert_update_key(const char *key, size_t key_len)
{
    ESP_LOGI(TAG, "Setting cert update key...");
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition("storage", CERT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_set_str(handle, "cert_update_key", key);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
