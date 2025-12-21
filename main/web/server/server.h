#ifndef SERVER_H
#define SERVER_H

#include "esp_http_server.h"
#include "esp_https_server.h"
#include "mdns.h"

httpd_handle_t start_http_redirect_server(void);
httpd_handle_t start_https_server(void);
void start_mdns_service(void);

extern httpd_handle_t http_server;
extern httpd_handle_t https_server;

#endif // SERVER_H
