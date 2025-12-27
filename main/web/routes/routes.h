#pragma once
#ifndef ROUTES_H
#define ROUTES_H

#include "esp_https_server.h"

typedef enum
{
    REDIRECT_COPY_IP,
    REDIRECT_LOGIN,
} redirect_type_t;

// HTTP routes
extern httpd_uri_t http_root;

// HTTPS routes
// No-auth routes
extern httpd_uri_t root;
extern httpd_uri_t ip;
extern httpd_uri_t copyIp;
extern httpd_uri_t login_get;
extern httpd_uri_t status_get;

// Protected routes
extern httpd_uri_t wol_get;
extern httpd_uri_t serviceCheck_get;

extern httpd_uri_t login_post;
extern httpd_uri_t ping_post;
extern httpd_uri_t serviceCheck_post;
extern httpd_uri_t wol_post;

#endif // ROUTES_H