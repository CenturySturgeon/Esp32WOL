#ifndef ROUTES_H
#define ROUTES_H

#include "esp_http_server.h"

esp_err_t http_redirect_handler(httpd_req_t *req);
esp_err_t home_handler(httpd_req_t *req);

#endif // ROUTES_H
