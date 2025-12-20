#ifndef SERVER_H
#define SERVER_H

#include "esp_http_server.h"
#include "esp_https_server.h"

httpd_handle_t start_http_redirect_server(void);
httpd_handle_t start_https_server(void);

#endif // SERVER_H
