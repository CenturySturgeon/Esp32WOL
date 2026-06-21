#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "server.h"
#include <time.h>
#include "esp_log.h"
#include "../handlers/handlers.h"
#include "../routes/routes.h"
#include "../../utils/nvs/nvs_utils.h"
#include "../../auth/auth.h"
#include "../../utils/telegram/queue.h"
// Certificate expiry checking using mbedtls
#include "mbedtls/x509_crt.h"

httpd_handle_t http_server = NULL;
httpd_handle_t https_server = NULL;

// Dynamic certificate buffers (loaded from NVS)
static uint8_t *dynamic_cert_buf = NULL;
static size_t dynamic_cert_len = 0;
static uint8_t *dynamic_key_buf = NULL;
static size_t dynamic_key_len = 0;

// Fallback embedded certificates (used if NVS load fails)
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
    // Admin endpoints (no session auth required bc of API key)
    &cert_status_get,
    &update_certs_post,
    // Protected routes
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
    // Load certificates (from NVS or fallback)
    uint8_t *cert_buf = NULL;
    size_t cert_len = 0;
    uint8_t *key_buf = NULL;
    size_t key_len = 0;

    esp_err_t err = load_certs_from_nvs(&cert_buf, &cert_len, &key_buf, &key_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load certificates");
        return NULL;
    }

    // Store the buffers for later cleanup
    dynamic_cert_buf = cert_buf;
    dynamic_key_buf = key_buf;
    dynamic_cert_len = cert_len;
    dynamic_key_len = key_len;

    httpd_handle_t server = start_https_server_with_certs(cert_buf, cert_len, key_buf, key_len);
    if (server == NULL)
    {
        // Don't free embedded certs
        if (cert_buf != (uint8_t *)server_crt_start)
        {
            free(cert_buf);
        }
        if (key_buf != (uint8_t *)server_key_start)
        {
            free(key_buf);
        }
    }
    return server;
}

httpd_handle_t start_https_server_with_certs(const uint8_t *cert, size_t cert_len,
                                             const uint8_t *key, size_t key_len)
{
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    // Link the certificates
    conf.servercert = cert;
    conf.servercert_len = cert_len;
    conf.prvtkey_pem = key;
    conf.prvtkey_len = key_len;

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

time_t get_certificate_expiry_time(const uint8_t *cert_der, size_t cert_len)
{
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);

    time_t expiry = 0;
    int err = mbedtls_x509_crt_parse(&crt, cert_der, cert_len);
    if (err == 0)
    {
        struct tm tm_time;
        memset(&tm_time, 0, sizeof(tm_time));

        // mbedTLS year is typically 4 digits, but handle 2 digit fallback safely
        unsigned int y = crt.valid_to.year;
        if (y < 100)
            y += 2000;

        tm_time.tm_year = y - 1900;            // struct tm expects years since 1900
        tm_time.tm_mon = crt.valid_to.mon - 1; // struct tm months are 0-11 (mbedTLS uses 1-12)
        tm_time.tm_mday = crt.valid_to.day;
        tm_time.tm_hour = crt.valid_to.hour;
        tm_time.tm_min = crt.valid_to.min;
        tm_time.tm_sec = crt.valid_to.sec;

        expiry = mktime(&tm_time);

        // Debug log to verify exactly what was parsed from the DER cert
        // ESP_LOGI(TAG, "Parsed cert expiry: %04d-%02d-%02d %02d:%02d:%02d (time_t=%ld)",
        //          y, crt.valid_to.mon, crt.valid_to.day,
        //          crt.valid_to.hour, crt.valid_to.min, crt.valid_to.sec, (long)expiry);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to parse certificate: -0x%04X", -err);
    }

    mbedtls_x509_crt_free(&crt);
    return expiry;
}

esp_err_t load_certs_from_nvs(uint8_t **cert_buf, size_t *cert_len, uint8_t **key_buf, size_t *key_len)
{
    // Try to load from NVS first
    esp_err_t err = nvs_load_certs(cert_buf, cert_len, key_buf, key_len);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded dynamic certificates from NVS");
        return ESP_OK;
    }

    // Fallback to embedded certificates
    ESP_LOGW(TAG, "NVS certs not available, using embedded fallback certs");
    *cert_buf = (uint8_t *)server_crt_start;
    *cert_len = server_crt_end - server_crt_start;
    *key_buf = (uint8_t *)server_key_start;
    *key_len = server_key_end - server_key_start;
    return ESP_OK; // Still return OK, just using fallback
}

void free_cert_buffers(uint8_t *cert_buf, uint8_t *key_buf)
{
    // Only free if they were dynamically allocated (not embedded)
    if (cert_buf && cert_buf != (uint8_t *)server_crt_start)
    {
        free(cert_buf);
    }
    if (key_buf && key_buf != (uint8_t *)server_key_start)
    {
        free(key_buf);
    }
}

#define CERT_EXPIRY_WARNING_DAYS 30

/**
 * Check certificate expiry and send Telegram alert if needed
 */
void check_certificate_expiry(void)
{
    ESP_LOGI(TAG, "Checking certificate expiry...");

    // Get current time once at the start
    time_t now = time(NULL);
    ESP_LOGI(TAG, "Current system time: %ld", (long)now);

    uint8_t *cert_buf = NULL;
    size_t cert_len = 0;
    uint8_t *key_buf = NULL;
    size_t key_len = 0;

    esp_err_t err = load_certs_from_nvs(&cert_buf, &cert_len, &key_buf, &key_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load certificates for expiry check");
        return;
    }

    time_t expiry = get_certificate_expiry_time(cert_buf, cert_len);

    if (expiry == 0)
    {
        ESP_LOGW(TAG, "Could not parse certificate expiry date");
        goto cleanup;
    }

    int days_remaining = (int)((expiry - now) / (24 * 60 * 60));

    // Log current status
    char expiry_str[32];
    struct tm *tm_info = gmtime(&expiry);
    strftime(expiry_str, sizeof(expiry_str), "%Y-%m-%d %H:%M:%S UTC", tm_info);
    ESP_LOGI(TAG, "Certificate expires on: %s (%d days remaining)", expiry_str, days_remaining);

    // Send alert if expiring within warning threshold
    if (days_remaining <= CERT_EXPIRY_WARNING_DAYS)
    {
        char alert_msg[256];
        snprintf(alert_msg, sizeof(alert_msg),
                 "Certificate expires in %d days (%s). Wake PC and run update_certs.py.",
                 days_remaining, expiry_str);

        // Use the existing Telegram queue infrastructure
        if (post_message_to_queue(alert_msg, false))
        {
            ESP_LOGI(TAG, "Certificate expiry alert sent to Telegram");
        }
        else
        {
            ESP_LOGW(TAG, "Failed to queue certificate expiry alert to Telegram");
        }
    }

cleanup:
    // Free dynamically allocated buffers (not embedded certs)
    free_cert_buffers(cert_buf, key_buf);
}

/**
 * Background task to safely stop/restart the HTTPS server.
 * Runs outside the HTTP handler context to avoid deadlocks.
 */
static void reload_https_server_task(void *pvParameters)
{
    // Allow time for the triggering request to complete and flush its response
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Reloading HTTPS server with new certificates...");

    if (auth_mutex != NULL)
    {
        xSemaphoreTake(auth_mutex, portMAX_DELAY);
    }

    // Free old certificate buffers
    free_cert_buffers(dynamic_cert_buf, dynamic_key_buf);
    dynamic_cert_buf = NULL;
    dynamic_key_buf = NULL;

    // Stop the HTTPS server gracefully (safe now that handler has returned)
    if (https_server != NULL)
    {
        ESP_LOGI(TAG, "Stopping current HTTPS server...");
        httpd_ssl_stop(https_server);
        https_server = NULL;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Load new certificates from NVS
    esp_err_t err = load_certs_from_nvs(&dynamic_cert_buf, &dynamic_cert_len,
                                        &dynamic_key_buf, &dynamic_key_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load certificates for reload");
        if (auth_mutex != NULL)
            xSemaphoreGive(auth_mutex);
        vTaskDelete(NULL);
        return;
    }

    // Start HTTPS server with new certificates
    https_server = start_https_server_with_certs(dynamic_cert_buf, dynamic_cert_len,
                                                 dynamic_key_buf, dynamic_key_len);
    if (https_server == NULL)
    {
        ESP_LOGE(TAG, "Failed to restart HTTPS server after cert reload");
    }

    if (auth_mutex != NULL)
        xSemaphoreGive(auth_mutex);

    ESP_LOGI(TAG, "HTTPS server reloaded successfully with new certificates");

    // Check certificate expiry immediately after reload
    check_certificate_expiry();

    vTaskDelete(NULL);
}

esp_err_t reload_https_server(void)
{
    BaseType_t ret = xTaskCreate(reload_https_server_task, "cert_reload", 4096, NULL, 5, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}