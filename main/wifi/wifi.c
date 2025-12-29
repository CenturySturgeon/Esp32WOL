#include <stdint.h>
#include <string.h>

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP-IDF System/WiFi
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

// Your project headers
#include "wifi.h"
#include "../auth/auth.h"
#include "./public_ip/public_ip.h"
#include "./ntp_sync/ntp_sync.h"
#include "../utils/telegram/queue.h"
#include "../utils/nvs/nvs_utils.h"

EventGroupHandle_t s_wifi_event_group;

static const char *TAG = "WIFI";

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_wifi_event_group)
        {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        ESP_LOGI(TAG, "Disconnected. Retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        if (s_wifi_event_group)
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }

        // Wake up the IP task immediately if it's sleeping
        if (public_ip_task_handle != NULL)
        {
            xTaskNotifyGive(public_ip_task_handle);
        }

        static bool ntp_task_started = false;
        if (!ntp_task_started)
        {
            ntp_task_started = true;
            xTaskCreate(ntp_management_task, "ntp_mgr", 8192, NULL, 5, NULL);
        }
    }
}

void wifi_init_sta(const char *ssid, const char *pass)
{
    initialize_notifications_queue();

    s_wifi_event_group = xEventGroupCreate(); // Create group now to avoid errors

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    esp_netif_ip_info_t ip_info;
    bool static_enabled = false;

    if (nvs_get_static_ip_config(&ip_info, &static_enabled) == ESP_OK && static_enabled)
    {
        // MUST stop DHCP client early
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif));

        // Set IP info
        ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));

        // Set the DNS server
        esp_netif_dns_info_t dns;
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_str_to_ip4("8.8.8.8", &dns.ip.u_addr.ip4);
        ESP_ERROR_CHECK(esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns));

        ESP_LOGI(TAG, "Static IP configured: " IPSTR, IP2STR(&ip_info.ip));
    }
    else
    {
        ESP_LOGI(TAG, "Using DHCP");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Using PSK for stability
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    if (ssid)
        strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass)
        strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}