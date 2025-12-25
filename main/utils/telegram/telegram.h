#ifndef TELEGRAM_H
#define TELEGRAM_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Sends a synchronous Telegram message.
 * * @param message The text to send
 * *@param silent  If true, sends without a notification sound
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t send_telegram_message_sync(const char *message, bool silent);

#endif // TELEGRAM_H