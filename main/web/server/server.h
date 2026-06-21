#pragma once
#ifndef SERVER_H
#define SERVER_H

#include "esp_http_server.h"

httpd_handle_t start_http_redirect_server(void);
httpd_handle_t start_https_server(void);
httpd_handle_t start_https_server_with_certs(const uint8_t *cert, size_t cert_len,
                                             const uint8_t *key, size_t key_len);
void start_mdns_service(void);

// Dynamic certificate management
esp_err_t load_certs_from_nvs(uint8_t **cert_buf, size_t *cert_len, uint8_t **key_buf, size_t *key_len);
esp_err_t reload_https_server(void);
void free_cert_buffers(uint8_t *cert_buf, uint8_t *key_buf);

// Certificate expiry checking
time_t get_certificate_expiry_time(const uint8_t *cert_der, size_t cert_len);
void check_certificate_expiry(void);

extern httpd_handle_t http_server;
extern httpd_handle_t https_server;

#endif // SERVER_H
