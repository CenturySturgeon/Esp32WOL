#include <sys/param.h>
#include <time.h>

#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

#include "handlers.h"
#include "../../auth/auth.h"
#include "../../utils/network/network.h"
#include "../routes/routes.h"
#include "../../utils/utils.h"
#include "../../utils/nvs/nvs_utils.h"
#include "../../web/server/server.h"

#include "../views/copyIp.h"
#include "../views/login.h"
#include "../views/service_check.h"
#include "../views/status.h"
#include "../views/wol.h"

extern const unsigned char server_crt_start[] asm("_binary_server_der_start");
extern const unsigned char server_crt_end[] asm("_binary_server_der_end");
extern const unsigned char server_key_start[] asm("_binary_server_key_der_start");
extern const unsigned char server_key_end[] asm("_binary_server_key_der_end");

extern char public_ip[64]; // Telling the compiler "trust me this exists somewhere (wifi.c)"

static const char *TAG = "ROUTE";

// Rate limiting for admin endpoints
#define MAX_CERT_UPDATE_ATTEMPTS 3
#define RATE_LIMIT_WINDOW_MS 6000000 // 1 hour

typedef struct
{
    int64_t timestamps[MAX_CERT_UPDATE_ATTEMPTS];
    int count;
} rate_limit_state_t;

static rate_limit_state_t cert_update_rate_limit = {0};

static bool is_rate_limited(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000; // Convert to milliseconds

    // Remove old timestamps outside the window
    int valid_count = 0;
    for (int i = 0; i < cert_update_rate_limit.count; i++)
    {
        if (now_ms - cert_update_rate_limit.timestamps[i] < RATE_LIMIT_WINDOW_MS)
        {
            cert_update_rate_limit.timestamps[valid_count++] = cert_update_rate_limit.timestamps[i];
        }
    }
    cert_update_rate_limit.count = valid_count;

    // Check if rate limited
    if (valid_count >= MAX_CERT_UPDATE_ATTEMPTS)
    {
        return true;
    }

    // Add new timestamp
    cert_update_rate_limit.timestamps[cert_update_rate_limit.count++] = now_ms;
    return false;
}

static esp_err_t _get_cookie_value(httpd_req_t *req, const char *cookie_name, char *val, size_t val_size)
{
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (hdr_len == 0)
        return ESP_FAIL;

    char *cookie_buf = malloc(hdr_len + 1);
    if (!cookie_buf)
        return ESP_ERR_NO_MEM;

    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, hdr_len + 1) != ESP_OK)
    {
        free(cookie_buf);
        return ESP_FAIL;
    }

    char *found = strstr(cookie_buf, cookie_name);
    if (!found)
    {
        free(cookie_buf);
        return ESP_FAIL;
    }

    // Move pointer to value
    found += strlen(cookie_name) + 1; // +1 for '='

    int i = 0;
    while (found[i] && found[i] != ';' && found[i] != ' ' && i < val_size - 1)
    {
        val[i] = found[i];
        i++;
    }
    val[i] = 0;

    free(cookie_buf);
    return ESP_OK;
}

esp_err_t http_redirect_handler(httpd_req_t *req)
{
    const int HTTPD_MAX_URI_LEN = 512;
    /* Host header length is limited by HTTPD */
    char host[HTTPD_MAX_URI_LEN] = {};

    /* "https://" + host + uri + null */
    char location[HTTPD_MAX_URI_LEN * 2] = {};

    /* Get Host header */
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK)
    {
        ESP_LOGW(TAG, "Host header missing, using default");
        strcpy(host, "esp32.local");
    }

    /* Build redirect URL safely */
    int len = snprintf(location, sizeof(location),
                       "https://%s%s",
                       host,
                       req->uri);

    if (len < 0 || len >= sizeof(location))
    {
        ESP_LOGE(TAG, "Redirect URL too long");
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "URI too long");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Redirecting to %s", location);

    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

esp_err_t https_redirect_handler(httpd_req_t *req)
{
    redirect_type_t type = *(redirect_type_t *)req->user_ctx;

    char host[128] = {};
    char location[256];

    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK)
    {
        strcpy(host, "esp32.local");
    }

    switch (type)
    {
    case REDIRECT_COPY_IP:
        snprintf(location, sizeof(location),
                 "https://%s/copyIp?ip=%s",
                 host, public_ip);
        break;

    case REDIRECT_LOGIN:
        snprintf(location, sizeof(location),
                 "https://%s/login",
                 host);
        break;

    default:
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Invalid redirect");
        return ESP_FAIL;
    }

    ESP_LOGI("HTTPS", "Redirecting to %s", location);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

esp_err_t get_copyIp_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, copyIp_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t get_login_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, login_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t get_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, status_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t post_login_handler(httpd_req_t *req)
{
    char content[128]; // Buffer for body (username=x&password=y)
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0'; // Null terminate

    // Simple parser for "username=...&password=..."
    char user[32] = {0};
    char pass[32] = {0};

    // Note: A robust URL decoder is recommended if you use special chars.
    // This assumes basic alphanumeric for brevity.
    char *p_user = strstr(content, "username=");
    char *p_pass = strstr(content, "password=");

    if (p_user && p_pass)
    {
        // Extract username
        p_user += 9; // len of "username="
        char *end_user = strchr(p_user, '&');
        if (end_user)
        {
            int len = MIN(end_user - p_user, sizeof(user) - 1);
            strncpy(user, p_user, len);
        }

        // Extract password
        p_pass += 9; // len of "password="
        // Password is usually the last field, reads until end or &
        char *end_pass = strchr(p_pass, '&');
        int len = end_pass ? MIN(end_pass - p_pass, sizeof(pass) - 1) : MIN(strlen(p_pass), sizeof(pass) - 1);
        strncpy(pass, p_pass, len);
    }

    ESP_LOGI(TAG, "Login attempt for user: %s", user);

    char session_token[33];
    uint8_t user_ttl;
    if (auth_login_user(user, pass, session_token, &user_ttl) == ESP_OK)
    {

        // Success: Set Cookie and Redirect
        char cookie_header[128];
        // Note: 'Secure' ensures it only travels via HTTPS. 'HttpOnly' hides it from JS.
        snprintf(cookie_header, sizeof(cookie_header),
                 "SESSIONID=%s; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age=%d",
                 session_token, user_ttl);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie_header);

        ESP_LOGI(TAG, "Login success. Redirecting to /wol");
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/wol");
        httpd_resp_send(req, NULL, 0);
    }
    else
    {
        ESP_LOGW(TAG, "Login failed.");
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_send(req, NULL, 0);
    }

    return ESP_OK;
}

// Protected Routes
esp_err_t get_wol_handler(httpd_req_t *req)
{
    char token[33];
    if (_get_cookie_value(req, "SESSIONID", token, sizeof(token)) == ESP_OK)
    {

        if (auth_check_session(token) == ESP_OK)
        {
            // Authorized
            ESP_LOGI(TAG, "Access granted to /wol");
            httpd_resp_set_type(req, "text/html");
            httpd_resp_send(req, wol_html, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
    }

    // Unauthorized
    ESP_LOGW(TAG, "Unauthorized access to /wol. Redirecting to Login.");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t get_service_check_handler(httpd_req_t *req)
{
    char token[33];
    if (_get_cookie_value(req, "SESSIONID", token, sizeof(token)) == ESP_OK)
    {

        if (auth_check_session(token) == ESP_OK)
        {
            // Authorized
            ESP_LOGI(TAG, "Access granted to /serviceCheck");
            httpd_resp_set_type(req, "text/html");
            httpd_resp_send(req, serviceCheck_html, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
    }

    // Unauthorized
    ESP_LOGW(TAG, "Unauthorized access to /serviceCheck. Redirecting to Login.");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t post_wol_handler(httpd_req_t *req)
{
    const size_t MAX_POST_SIZE = 256;
    char content[MAX_POST_SIZE];

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= MAX_POST_SIZE)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, content, total_len);
    if (received <= 0)
        return ESP_FAIL;
    content[received] = '\0';

    // ESP_LOGI(TAG, "Full POST Body: %s", content);

    char mac[32] = {0}, secure[32] = {0}, broadcast[32] = {0}, pin_str[16] = {0};

    // Parse manually
    // Split the string by '&' and then look for key=value
    char *buf = content;
    char *token = strtok_r(buf, "&", &buf);
    while (token != NULL)
    {
        if (strncmp(token, "macAddress=", 11) == 0)
        {
            strncpy(mac, token + 11, sizeof(mac) - 1);
        }
        else if (strncmp(token, "secureOn=", 9) == 0)
        {
            strncpy(secure, token + 9, sizeof(secure) - 1);
        }
        else if (strncmp(token, "broadcastAddress=", 17) == 0)
        {
            strncpy(broadcast, token + 17, sizeof(broadcast) - 1);
        }
        else if (strncmp(token, "pin=", 4) == 0)
        {
            strncpy(pin_str, token + 4, sizeof(pin_str) - 1);
        }
        token = strtok_r(NULL, "&", &buf);
    }

    // Clean up URL encoding (%3A -> :)
    url_decode(mac);
    url_decode(secure);
    url_decode(broadcast);
    url_decode(pin_str);

    // ESP_LOGI(TAG, "Parsed Results -> MAC: %s, PIN: %s, Broadcast: %s", mac, pin_str, broadcast);

    char session_token[33];
    uint32_t pin_code = strtoul(pin_str, NULL, 10);

    if (_get_cookie_value(req, "SESSIONID", session_token, sizeof(session_token)) == ESP_OK)
    {
        if (auth_check_session(session_token) == ESP_OK)
        {
            if (auth_check_totp_request(session_token, pin_code) == ESP_OK)
            {

                ESP_LOGI(TAG, "Success! Sending WOL packet");
                auth_logout_user(session_token);
                send_wol_packet(mac, secure, broadcast);

                httpd_resp_set_status(req, "303 See Other");
                httpd_resp_set_hdr(req, "Location", "/status?s=success");
                httpd_resp_send(req, NULL, 0);
                return ESP_OK;
            }
        }
    }

    // ESP_LOGW(TAG, "Auth failed or PIN incorrect");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/status?s=error");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t post_ping_handler(httpd_req_t *req)
{
    const size_t MAX_POST_SIZE = 128;
    char content[MAX_POST_SIZE];

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= MAX_POST_SIZE)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, content, total_len);
    if (received <= 0)
        return ESP_FAIL;
    content[received] = '\0';

    // ESP_LOGI(TAG, "Full POST Body: %s", content);

    char pin_str[16] = {0};

    // Parse manually
    // Split the string by '&' and then look for key=value
    char *buf = content;
    char *token = strtok_r(buf, "&", &buf);
    while (token != NULL)
    {
        if (strncmp(token, "pin=", 4) == 0)
        {
            strncpy(pin_str, token + 4, sizeof(pin_str) - 1);
        }
        token = strtok_r(NULL, "&", &buf);
    }

    // Clean up URL encoding (%3A -> :)
    url_decode(pin_str);

    // ESP_LOGI(TAG, "Parsed Results -> PIN: %s", pin_str);

    char session_token[33];
    uint32_t pin_code = strtoul(pin_str, NULL, 10);

    if (_get_cookie_value(req, "SESSIONID", session_token, sizeof(session_token)) == ESP_OK)
    {
        if (auth_check_session(session_token) == ESP_OK)
        {
            if (auth_check_totp_request(session_token, pin_code) == ESP_OK)
            {

                ESP_LOGI(TAG, "Success! Pinging hosts");
                auth_logout_user(session_token);

                // Ping hosts and send telegram message
                network_ping_all_hosts();

                httpd_resp_set_status(req, "303 See Other");
                httpd_resp_set_hdr(req, "Location", "/status?s=success");
                httpd_resp_send(req, NULL, 0);
                return ESP_OK;
            }
        }
    }

    // ESP_LOGW(TAG, "Auth failed or PIN incorrect");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/status?s=error");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t post_serviceCheck_handler(httpd_req_t *req)
{
    const size_t MAX_POST_SIZE = 128;
    char content[MAX_POST_SIZE];

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= MAX_POST_SIZE)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, content, total_len);
    if (received <= 0)
        return ESP_FAIL;
    content[received] = '\0';

    // ESP_LOGI(TAG, "Full POST Body: %s", content);

    char pin_str[16] = {0};

    // Parse manually
    // Split the string by '&' and then look for key=value
    char *buf = content;
    char *token = strtok_r(buf, "&", &buf);
    while (token != NULL)
    {
        if (strncmp(token, "pin=", 4) == 0)
        {
            strncpy(pin_str, token + 4, sizeof(pin_str) - 1);
        }
        token = strtok_r(NULL, "&", &buf);
    }

    // Clean up URL encoding (%3A -> :)
    url_decode(pin_str);

    // ESP_LOGI(TAG, "Parsed Results -> PIN: %s", pin_str);

    char session_token[33];
    uint32_t pin_code = strtoul(pin_str, NULL, 10);

    if (_get_cookie_value(req, "SESSIONID", session_token, sizeof(session_token)) == ESP_OK)
    {
        if (auth_check_session(session_token) == ESP_OK)
        {
            if (auth_check_totp_request(session_token, pin_code) == ESP_OK)
            {

                ESP_LOGI(TAG, "Success! Checking services..");
                auth_logout_user(session_token);

                // Scan host's services & send message to queue
                network_scan_services();

                httpd_resp_set_status(req, "303 See Other");
                httpd_resp_set_hdr(req, "Location", "/status?s=success");
                httpd_resp_send(req, NULL, 0);
                return ESP_OK;
            }
        }
    }

    // ESP_LOGW(TAG, "Auth failed or PIN incorrect");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/status?s=error");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Admin Endpoints
esp_err_t get_cert_status_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /admin/cert-status");

    // Load certificates to check expiry
    uint8_t *cert_buf = NULL;
    size_t cert_len = 0;
    uint8_t *key_buf = NULL;
    size_t key_len = 0;

    esp_err_t err = load_certs_from_nvs(&cert_buf, &cert_len, &key_buf, &key_len);
    if (err != ESP_OK)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"Failed to load certificates\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    time_t expiry = get_certificate_expiry_time(cert_buf, cert_len);
    time_t now = time(NULL);
    int days_remaining = (expiry - now) / (24 * 60 * 60);

    // Format expiry date
    char expiry_str[20];
    struct tm *tm_info = gmtime(&expiry);
    strftime(expiry_str, sizeof(expiry_str), "%Y-%m-%d", tm_info);

    const char *source = nvs_has_nvs_certs() ? "nvs" : "embedded";

    // Create JSON response
    cJSON *json = cJSON_CreateObject();
    if (!json)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"JSON creation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(json, "expiry", expiry_str);
    cJSON_AddNumberToObject(json, "days_remaining", days_remaining);
    cJSON_AddStringToObject(json, "source", source);

    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!json_str)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"JSON serialization failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);

    // Don't free embedded certs
    if (cert_buf != (uint8_t *)server_crt_start)
    {
        free(cert_buf);
    }
    if (key_buf != (uint8_t *)server_key_start)
    {
        free(key_buf);
    }

    return ESP_OK;
}

esp_err_t post_update_certs_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /admin/update-certs");

    // Check rate limiting first
    if (is_rate_limited())
    {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_send(req, "{\"error\": \"Rate limit exceeded. Max 3 attempts per minute.\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Check X-Cert-Key header
    size_t key_hdr_len = httpd_req_get_hdr_value_len(req, "X-Cert-Key");
    if (key_hdr_len == 0 || key_hdr_len >= 128)
    {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\": \"Missing or invalid X-Cert-Key header\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char *cert_key = malloc(key_hdr_len + 1);
    if (!cert_key)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    if (httpd_req_get_hdr_value_str(req, "X-Cert-Key", cert_key, key_hdr_len + 1) != ESP_OK)
    {
        free(cert_key);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\": \"Failed to read X-Cert-Key header\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    cert_key[key_hdr_len] = '\0';

    // Get stored cert update key from NVS
    char stored_key[128] = {0};
    esp_err_t err = nvs_get_cert_update_key(stored_key, sizeof(stored_key));
    if (err != ESP_OK || strcmp(cert_key, stored_key) != 0)
    {
        free(cert_key);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\": \"Invalid certificate update key\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    free(cert_key);

    // Parse JSON payload
    const size_t MAX_JSON_SIZE = 4096;
    char *json_buf = malloc(MAX_JSON_SIZE);
    if (!json_buf)
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= MAX_JSON_SIZE)
    {
        free(json_buf);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\": \"Invalid content length\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, json_buf, total_len);
    if (received <= 0)
    {
        free(json_buf);
        return ESP_FAIL;
    }
    json_buf[received] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(json_buf);
    free(json_buf);

    if (!json)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\": \"Invalid JSON payload\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Extract base64 encoded cert and key
    cJSON *cert_b64 = cJSON_GetObjectItem(json, "certificate");
    cJSON *key_b64 = cJSON_GetObjectItem(json, "private_key");

    if (!cJSON_IsString(cert_b64) || !cJSON_IsString(key_b64))
    {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\": \"Missing certificate or private_key fields\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Decode base64
    size_t cert_decoded_len = 0;
    uint8_t *cert_der = NULL;
    int ret = mbedtls_base64_decode(NULL, 0, &cert_decoded_len, (const unsigned char *)cert_b64->valuestring, strlen(cert_b64->valuestring));
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && ret != 0)
    {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\": \"Invalid certificate base64 encoding\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cert_der = malloc(cert_decoded_len);
    if (!cert_der)
    {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ret = mbedtls_base64_decode(cert_der, cert_decoded_len, &cert_decoded_len, (const unsigned char *)cert_b64->valuestring, strlen(cert_b64->valuestring));
    if (ret != 0)
    {
        free(cert_der);
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\": \"Certificate base64 decoding failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    size_t key_decoded_len = 0;
    uint8_t *key_der = NULL;
    ret = mbedtls_base64_decode(NULL, 0, &key_decoded_len, (const unsigned char *)key_b64->valuestring, strlen(key_b64->valuestring));
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && ret != 0)
    {
        free(cert_der);
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\": \"Invalid key base64 encoding\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    key_der = malloc(key_decoded_len);
    if (!key_der)
    {
        free(cert_der);
        cJSON_Delete(json);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ret = mbedtls_base64_decode(key_der, key_decoded_len, &key_decoded_len, (const unsigned char *)key_b64->valuestring, strlen(key_b64->valuestring));
    if (ret != 0)
    {
        free(cert_der);
        free(key_der);
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"error\": \"Private key base64 decoding failed\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON_Delete(json);

    // Save to NVS
    err = nvs_save_certs(cert_der, cert_decoded_len, key_der, key_decoded_len);
    if (err != ESP_OK)
    {
        free(cert_der);
        free(key_der);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\": \"Failed to save certificates to NVS\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    free(cert_der);
    free(key_der);

    // Reload HTTPS server with new certificates
    err = reload_https_server();
    if (err != ESP_OK)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "{\"error\": \"Failed to reload HTTPS server\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Send success response
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, "{\"message\": \"Certificates updated successfully\"}", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Certificates updated and server reloaded successfully");
    return ESP_OK;
}