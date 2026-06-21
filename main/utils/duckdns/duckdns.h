#ifndef DUCKDNS_H
#define DUCKDNS_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t duckdns_update_sync(const char *ip);
esp_err_t duckdns_update_with_retry(const char *ip);

#endif
