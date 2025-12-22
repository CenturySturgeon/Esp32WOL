#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_log.h"
#ifndef TOTP_H
#define TOTP_H

bool totp_verify(
    const uint8_t *key,
    size_t key_len,
    uint32_t user_code);

#endif // TOTP_H