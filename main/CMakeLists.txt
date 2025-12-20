idf_component_register(
    SRCS "main.c"
         "auth/auth.c"
         "wifi/wifi.c"
    INCLUDE_DIRS "."
    REQUIRES esp_https_server nvs_flash esp_netif esp_wifi esp_http_client
)
