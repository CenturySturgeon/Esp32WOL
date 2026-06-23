#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "../auth/auth.h"

#include "esp_log.h"
#include "esp_err.h"
#include <ctype.h>

static const char *TAG = "UTILS";

// Decodes html urls (hmac's ":" are encoded to "%3A")
void url_decode(char *src)
{
    ESP_LOGD(TAG, "URL decode...");
    char *dst = src;
    while (*src)
    {
        if (*src == '%' && src[1] && src[2])
        {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void bytes_to_hex(const unsigned char *src, char *dest, int len)
{
    ESP_LOGD(TAG, "Converting bytes to HEX...");
    for (int i = 0; i < len; i++)
    {
        sprintf(dest + (i * 2), "%02x", src[i]);
    }
    dest[len * 2] = 0;
}

/**
 * Converts a null-terminated hex string into a binary byte array.
 * Each pair of hex characters is converted to one byte.
 * 
 * @param hex_str Input hex string (e.g., "a1b2c3d4")
 * @param output Output buffer for binary data
 * @param max_len Maximum length of output buffer
 * @return Number of bytes written, or -1 on error
 */
int hex_to_bin(const char *hex_str, unsigned char *output, int max_len)
{
    if (!hex_str || !output || max_len <= 0)
        return -1;

    size_t hex_len = strlen(hex_str);
    
    // Hex string must have even length
    if (hex_len % 2 != 0)
    {
        ESP_LOGE(TAG, "Invalid hex string: odd length");
        return -1;
    }

    size_t bin_len = hex_len / 2;
    
    // Check buffer overflow
    if (bin_len > (size_t)max_len)
    {
        ESP_LOGE(TAG, "Hex string too long for output buffer");
        return -1;
    }

    for (size_t i = 0; i < bin_len; i++)
    {
        char hex_pair[3] = {hex_str[i * 2], hex_str[i * 2 + 1], '\0'};
        
        // Validate hex characters
        if (!isxdigit(hex_pair[0]) || !isxdigit(hex_pair[1]))
        {
            ESP_LOGE(TAG, "Invalid hex character at position %zu", i * 2);
            return -1;
        }
        
        unsigned long byte_val = strtoul(hex_pair, NULL, 16);
        output[i] = (unsigned char)byte_val;
    }

    return (int)bin_len;
}
