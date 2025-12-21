#include "server.h"
#include "routes.h"
#include "esp_log.h"

httpd_handle_t http_server = NULL;
httpd_handle_t https_server = NULL;
extern const unsigned char server_crt_start[] asm("_binary_server_der_start");
extern const unsigned char server_crt_end[] asm("_binary_server_der_end");

extern const unsigned char server_key_start[] asm("_binary_server_key_der_start");
extern const unsigned char server_key_end[] asm("_binary_server_key_der_end");

static const char *TAG = "SERVER";

httpd_handle_t start_http_redirect_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t http_server = NULL;

    if (httpd_start(&http_server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);

        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = http_redirect_handler,
            .user_ctx = NULL};

        httpd_register_uri_handler(http_server, &root);
    }

    return http_server;
}

httpd_handle_t start_https_server(void)
{
    httpd_handle_t https_server = NULL;
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    // Link the embedded certificates
    conf.servercert = server_crt_start;
    conf.servercert_len = server_crt_end - server_crt_start;

    conf.prvtkey_pem = server_key_start;
    conf.prvtkey_len = server_key_end - server_key_start;

    // Optional: Set the port (Default is 443)
    // conf.httpd.port_secure = 443;

    if (httpd_ssl_start(&https_server, &conf) == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTPS server started on port %d", conf.port_secure);

        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = home_handler,
            .user_ctx = NULL};

        httpd_register_uri_handler(https_server, &root);
        return https_server;
    }

    ESP_LOGE(TAG, "Error starting HTTPS server!");
    return NULL;
}

void start_mdns_service(void)
{
    esp_err_t err;

    // Initialize mDNS
    err = mdns_init();
    if (err)
    {
        ESP_LOGE(TAG, "MDNS Init failed: %d", err);
        return;
    }

    // Set hostname (this is the "esp32.local" part)
    err = mdns_hostname_set("esp32"); // So devices can reach it via esp32.local
    if (err)
    {
        ESP_LOGE(TAG, "MDNS hostname set failed: %d", err);
    }

    // Set instance name (friendly name for discovery apps)
    mdns_instance_name_set("ESP32 HTTPS Server");

    ESP_LOGI(TAG, "mDNS started, hostname=esp32.local");
}