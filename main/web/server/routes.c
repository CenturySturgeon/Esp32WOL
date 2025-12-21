#include "routes.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"

#include "../views/copyIp.h"
#include "../views/home.h"
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

esp_err_t home_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}