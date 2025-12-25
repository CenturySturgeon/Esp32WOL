idf_component_register(
    SRCS "main.c"
         "auth/auth.c"
         "auth/totp.c"
         "utils/ipify/ipify.c"
         "utils/network/network.c"
         "utils/telegram/telegram.c"
         "utils/telegram/queue.c"
         "utils/utils.c"
         "web/server/handlers.c"
         "web/server/server.c"
         "wifi/wifi.c"
    INCLUDE_DIRS "."
    REQUIRES esp_https_server nvs_flash esp_netif esp_wifi esp_http_client mdns esp_timer
    EMBED_FILES
        "web/certs/api_ipify.pem"
        "web/certs/server.der"
        "web/certs/server_key.der"
        "web/certs/telegram.pem"
)
