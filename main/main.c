#include <stdio.h>
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "./utils/led/led_utils.h"
#include "./utils/nvs/nvs_utils.h"
#include "./wifi/wifi.h"

/* -------- APP MAIN -------- */
static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application");

    // Initialize system stacks
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize led blinker
    led_utils_init();
    led_utils_set_blinks(1); // Blink once per cycle

    // Initialize custom NVS storage
    nvs_init_and_load_secrets();

    // Temporary local buffers
    char temp_ssid[32] = {0};
    char temp_pass[64] = {0};

    // Load from NVS into temporary buffers
    if (nvs_get_wifi_credentials(temp_ssid, sizeof(temp_ssid), temp_pass, sizeof(temp_pass)) == ESP_OK)
    {

        // Start Wi-Fi with these credentials
        wifi_init_sta(temp_ssid, temp_pass);

        // SECURITY: Immediately wipe the buffers from RAM
        memset(temp_ssid, 0, sizeof(temp_ssid));
        memset(temp_pass, 0, sizeof(temp_pass));
        ESP_LOGI(TAG, "Wi-Fi credentials cleared from app RAM.");
    }
    else
    {
        ESP_LOGE(TAG, "Could not load Wi-Fi credentials from NVS storage partition.");
    }
}