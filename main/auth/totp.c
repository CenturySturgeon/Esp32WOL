
#include "totp.h"
#include "mbedtls/md.h"
#include "time.h"

static const char *TAG = "TOTP";

static uint32_t totp_generate(
    const uint8_t *key,
    size_t key_len,
    uint64_t counter)
{
    uint8_t counter_be[8];
    uint8_t hmac[20];

    // Convert counter to big-endian
    for (int i = 7; i >= 0; i--)
    {
        counter_be[i] = counter & 0xFF;
        counter >>= 8;
    }

    const mbedtls_md_info_t *md =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);

    mbedtls_md_hmac(
        md,
        key,
        key_len,
        counter_be,
        sizeof(counter_be),
        hmac);

    // Dynamic truncation
    uint8_t offset = hmac[19] & 0x0F;
    uint32_t code =
        ((hmac[offset] & 0x7F) << 24) |
        (hmac[offset + 1] << 16) |
        (hmac[offset + 2] << 8) |
        (hmac[offset + 3]);

    return code % 1000000; // 6-digit TOTP
}

bool totp_verify(
    const uint8_t *key,
    size_t key_len,
    uint32_t user_code)
{
    time_t now = time(NULL);
    uint64_t counter = now / 30;

    uint32_t expected = totp_generate(key, key_len, counter);
    ESP_LOGI(TAG, "Verifying TOTP pin");
    // ESP_LOGI(TAG, "Expected TOTP: %u", expected);
    if (expected == user_code)
    {
        return true;
    }

    return false;
}
