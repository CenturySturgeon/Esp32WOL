idf_component_register(
    SRCS "main.c"
         "auth/auth.c"
         "web/server/routes.c"
         "web/server/server.c"
         "wifi/wifi.c"
    INCLUDE_DIRS "."
    REQUIRES esp_https_server nvs_flash esp_netif esp_wifi esp_http_client mdns
    EMBED_FILES
        "web/certs/server.der"
        "web/certs/server_key.der"
        "web/certs/api_ipify.pem"
)
