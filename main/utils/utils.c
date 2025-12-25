#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "../auth/auth.h"

#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "UTILS";

// Decodes html urls (hmac's ":" are encoded to "%3A")
void url_decode(char *src)
{
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
    for (int i = 0; i < len; i++)
    {
        sprintf(dest + (i * 2), "%02x", src[i]);
    }
    dest[len * 2] = 0;
}
