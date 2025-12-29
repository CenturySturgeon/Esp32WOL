#pragma once
#ifndef WIFI_H
#define WIFI_H

#include "esp_event.h"
#include <stdint.h>

extern EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void wifi_init_sta(const char *ssid, const char *pass);

#endif // WIFI_H