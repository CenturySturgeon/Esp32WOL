#ifndef WIFI_H
#define WIFI_H

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "nvs.h"
#include <time.h>
#include <sys/time.h>

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void wifi_init_sta(const char *ssid, const char *pass);
// void ntp_sync_time(void);

#endif // WIFI_H