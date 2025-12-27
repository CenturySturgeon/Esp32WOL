#pragma once
#ifndef HANDLERS_H
#define HANDLERS_H

#include "esp_http_server.h"

// Redirects
esp_err_t http_redirect_handler(httpd_req_t *req);
esp_err_t https_redirect_handler(httpd_req_t *req);

// GET handlers
esp_err_t get_copyIp_handler(httpd_req_t *req);
esp_err_t get_login_handler(httpd_req_t *req);
esp_err_t get_status_handler(httpd_req_t *req);
esp_err_t get_wol_handler(httpd_req_t *req);

// POST handlers
esp_err_t post_login_handler(httpd_req_t *req);
esp_err_t post_wol_handler(httpd_req_t *req);

typedef enum
{
    REDIRECT_COPY_IP,
    REDIRECT_LOGIN,
} redirect_type_t;

#endif // HANDLERS_H