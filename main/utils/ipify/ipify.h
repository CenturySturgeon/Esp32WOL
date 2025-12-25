#pragma once
#ifndef IPIFY_H
#define IPIFY_H

#include "esp_err.h"

esp_err_t get_public_ip(char *ip_buf, size_t ip_buf_len);

#endif // IPIFY_H