#ifndef ROUTES_H
#define ROUTES_H

#include "esp_http_server.h"

esp_err_t http_redirect_handler(httpd_req_t *req);
esp_err_t https_redirect_handler(httpd_req_t *req);
esp_err_t login_handler(httpd_req_t *req);
esp_err_t copyIp_handler(httpd_req_t *req);

typedef enum {
    REDIRECT_COPY_IP,
    REDIRECT_LOGIN,
} redirect_type_t;


#endif // ROUTES_H
