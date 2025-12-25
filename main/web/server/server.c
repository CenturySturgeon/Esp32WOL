#include "server.h"
#include "esp_log.h"
#include "../handlers/handlers.h"

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

static redirect_type_t redirect_copy_ip = REDIRECT_COPY_IP;
static redirect_type_t redirect_login = REDIRECT_LOGIN;

static const char *TAG = "SERVER";

httpd_handle_t start_http_redirect_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // URL widlcard matching
    config.uri_match_fn = httpd_uri_match_wildcard;

    // Enable purging of old connections
    config.lru_purge_enable = LRU_PURGE;
    config.max_open_sockets = MAX_HTTP_SOCKETS;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP redirect server started on port %d", config.server_port);

        httpd_uri_t root = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = http_redirect_handler,
            .user_ctx = NULL};

        httpd_register_uri_handler(server, &root);
        return server;
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

    if (httpd_ssl_start(&https_server, &conf) == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTPS server started on port %d", conf.port_secure);

        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = https_redirect_handler,
            .user_ctx = &redirect_login};

        httpd_uri_t ip = {
            .uri = "/ip",
            .method = HTTP_GET,
            .handler = https_redirect_handler,
            .user_ctx = &redirect_copy_ip};

        httpd_uri_t copyIp = {
            .uri = "/copyIp",
            .method = HTTP_GET,
            .handler = get_copyIp_handler,
            .user_ctx = NULL};

        httpd_uri_t login_get = {
            .uri = "/login",
            .method = HTTP_GET,
            .handler = get_login_handler,
            .user_ctx = NULL};

        httpd_uri_t status_get = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = get_status_handler,
            .user_ctx = NULL};

        // Protected
        httpd_uri_t wol_get = {
            .uri = "/wol",
            .method = HTTP_GET,
            .handler = get_wol_handler,
            .user_ctx = NULL};

        httpd_uri_t login_post = {
            .uri = "/login",
            .method = HTTP_POST,
            .handler = post_login_handler,
            .user_ctx = NULL};

        httpd_uri_t wol_post = {
            .uri = "/wol",
            .method = HTTP_POST,
            .handler = post_wol_handler,
            .user_ctx = NULL};

        httpd_register_uri_handler(https_server, &root);
        httpd_register_uri_handler(https_server, &ip);
        httpd_register_uri_handler(https_server, &copyIp);
        httpd_register_uri_handler(https_server, &login_get);
        httpd_register_uri_handler(https_server, &status_get);
        httpd_register_uri_handler(https_server, &wol_get);

        httpd_register_uri_handler(https_server, &login_post);
        httpd_register_uri_handler(https_server, &wol_post);

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