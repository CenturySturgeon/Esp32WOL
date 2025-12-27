#include "esp_http_server.h"
#include "esp_https_server.h"

#include "routes.h"
#include "../handlers/handlers.h"

static redirect_type_t redirect_copy_ip = REDIRECT_COPY_IP;
static redirect_type_t redirect_login = REDIRECT_LOGIN;

// HTTP handlers
httpd_uri_t http_root = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = http_redirect_handler, // HTTP is redirection only
    .user_ctx = NULL};

// HTTPS handlers
httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = https_redirect_handler,
    .user_ctx = &redirect_login};

httpd_uri_t ip = {
    .uri = "/ip",
    .method = HTTP_GET,
    .handler = https_redirect_handler,
    .user_ctx = &redirect_copy_ip};

httpd_uri_t copyIp = {
    .uri = "/copyIp",
    .method = HTTP_GET,
    .handler = get_copyIp_handler,
    .user_ctx = NULL};

httpd_uri_t login_get = {
    .uri = "/login",
    .method = HTTP_GET,
    .handler = get_login_handler,
    .user_ctx = NULL};

httpd_uri_t status_get = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = get_status_handler,
    .user_ctx = NULL};

// Protected route handlers
httpd_uri_t wol_get = {
    .uri = "/wol",
    .method = HTTP_GET,
    .handler = get_wol_handler,
    .user_ctx = NULL};

httpd_uri_t serviceCheck_get = {
    .uri = "/serviceCheck",
    .method = HTTP_GET,
    .handler = get_service_check_handler,
    .user_ctx = NULL};

httpd_uri_t login_post = {
    .uri = "/login",
    .method = HTTP_POST,
    .handler = post_login_handler,
    .user_ctx = NULL};

httpd_uri_t ping_post = {
    .uri = "/ping",
    .method = HTTP_POST,
    .handler = post_ping_handler,
    .user_ctx = NULL};

httpd_uri_t serviceCheck_post = {
    .uri = "/serviceCheck",
    .method = HTTP_POST,
    .handler = post_serviceCheck_handler,
    .user_ctx = NULL};

httpd_uri_t wol_post = {
    .uri = "/wol",
    .method = HTTP_POST,
    .handler = post_wol_handler,
    .user_ctx = NULL};