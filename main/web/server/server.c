#include "server.h"
#include "esp_log.h"
#include "../handlers/handlers.h"
#include "../routes/routes.h"

httpd_handle_t http_server = NULL;
httpd_handle_t https_server = NULL;

extern const unsigned char server_crt_start[] asm("_binary_server_der_start");
extern const unsigned char server_crt_end[] asm("_binary_server_der_end");
extern const unsigned char server_key_start[] asm("_binary_server_key_der_start");
extern const unsigned char server_key_end[] asm("_binary_server_key_der_end");

// Web server TLS & connection params
static uint8_t MAX_HTTP_SOCKETS = 5;
static uint8_t MAX_HTTPS_SOCKETS = 7;
static uint8_t TCP_HANDSHAKE_LINGER_TIMEOUT = 90; // Keep connections alive for less time to free resources faster
static bool LRU_PURGE = true;                     // Purge connections with LRU algo

// HTTP handlers
static const httpd_uri_t *http_uri_handlers[] = {
    &http_root,
};

// HTTPS handlers
static const httpd_uri_t *https_uri_handlers[] = {
    &root,
    &ip,
    &copyIp,
    &login_get,
    &status_get,
    &serviceCheck_get,
    &wol_get,
    &login_post,
    &ping_post,
    &serviceCheck_post,
    &wol_post,
};

static const size_t HTTP_URI_COUNT = sizeof(http_uri_handlers) / sizeof(http_uri_handlers[0]);
static const size_t HTTPS_URI_COUNT = sizeof(https_uri_handlers) / sizeof(https_uri_handlers[0]);

static const char *TAG = "SERVER";

httpd_handle_t start_http_redirect_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // URL widlcard matching
    config.uri_match_fn = httpd_uri_match_wildcard;

    // Enable purging of old connections
    config.lru_purge_enable = LRU_PURGE;
    config.max_open_sockets = MAX_HTTP_SOCKETS;

    httpd_handle_t http_server = NULL;

    if (httpd_start(&http_server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP redirect server started on port %d", config.server_port);

        for (size_t i = 0; i < HTTP_URI_COUNT; i++)
        {
            esp_err_t err = httpd_register_uri_handler(http_server, http_uri_handlers[i]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG,
                         "Failed to register URI: %s (%s)",
                         http_uri_handlers[i]->uri,
                         esp_err_to_name(err));
            }
        }

        return http_server;
    }

    ESP_LOGE(TAG, "Error starting HTTP redirect server!");
    return NULL;
}

httpd_handle_t start_https_server(void)
{
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    // Link the embedded certificates
    conf.servercert = server_crt_start;
    conf.servercert_len = server_crt_end - server_crt_start;
    conf.prvtkey_pem = server_key_start;
    conf.prvtkey_len = server_key_end - server_key_start;

    conf.httpd.max_open_sockets = MAX_HTTPS_SOCKETS;
    conf.httpd.lru_purge_enable = LRU_PURGE;
    conf.httpd.linger_timeout = TCP_HANDSHAKE_LINGER_TIMEOUT;
    conf.httpd.max_uri_handlers = HTTP_URI_COUNT + HTTPS_URI_COUNT;

    if (httpd_ssl_start(&https_server, &conf) == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTPS server started on port %d", conf.port_secure);

        for (size_t i = 0; i < HTTPS_URI_COUNT; i++)
        {
            esp_err_t err = httpd_register_uri_handler(https_server, https_uri_handlers[i]);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG,
                         "Failed to register URI: %s (%s)",
                         https_uri_handlers[i]->uri,
                         esp_err_to_name(err));
            }
        }

        return https_server;
    }

    ESP_LOGE(TAG, "Error starting HTTPS server!");
    return NULL;
}

void start_mdns_service(void)
{
    esp_err_t err = mdns_init();
    if (err)
    {
        ESP_LOGE(TAG, "MDNS Init failed: %d", err);
        return;
    }

    // Set hostname (this is the "esp32.local" part)
    err = mdns_hostname_set("esp32"); // So LAN devices can reach it via esp32.local
    if (err)
    {
        ESP_LOGE(TAG, "MDNS hostname set failed: %d", err);
    }

    // Set instance name (friendly name for discovery apps)
    mdns_instance_name_set("ESP32 HTTPS Server");
    ESP_LOGI(TAG, "mDNS started, hostname=esp32.local");
}