#include "handlers.h"
#include "../../auth/auth.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include <sys/param.h>

#include "../views/copyIp.h"
#include "../views/login.h"
#include "../views/status.h"
#include "../views/wol.h"

extern char public_ip[64]; // Telling the compiler "trust me this exists somewhere (wifi.c)"

static const char *TAG = "ROUTE";

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

esp_err_t copyIp_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, copyIp_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t login_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, login_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t login_post_handler(httpd_req_t *req)
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
    if (auth_login_user(user, pass, session_token) == ESP_OK)
    {

        // Success: Set Cookie and Redirect
        char cookie_header[128];
        // Note: 'Secure' ensures it only travels via HTTPS. 'HttpOnly' hides it from JS.
        snprintf(cookie_header, sizeof(cookie_header), "SESSIONID=%s; Path=/; Secure; HttpOnly", session_token);
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

// Protected Route
esp_err_t wol_handler(httpd_req_t *req)
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