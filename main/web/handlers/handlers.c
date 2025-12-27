#include <sys/param.h>

#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"

#include "handlers.h"
#include "../../auth/auth.h"
#include "../../utils/network/network.h"
#include "../routes/routes.h"
#include "../../utils/utils.h"

#include "../views/copyIp.h"
#include "../views/login.h"
#include "../views/service_check.h"
#include "../views/status.h"
#include "../views/wol.h"

extern char public_ip[64]; // Telling the compiler "trust me this exists somewhere (wifi.c)"

static const char *TAG = "ROUTE";

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
        // Return 401 or Redirect back to login
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid Credentials");
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
                // Ping hosts here

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
                // Check services here

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
