#pragma once
#ifndef LED_UTILS_H
#define LED_UTILS_H

#include <stdint.h>

// Built-in LED GPIO
#define LED_GPIO_PIN      GPIO_NUM_2

// LED logic level
#define LED_ACTIVE_LEVEL  1   // 1 = ON, 0 = OFF

// Default timings (milliseconds)
#define LED_DEFAULT_BLINK_ON_MS     300
#define LED_DEFAULT_BLINK_OFF_MS    300
#define LED_DEFAULT_CYCLE_PAUSE_MS  1500


/**
 * @brief Initialize LED GPIO and start LED timer
 */
void led_utils_init(void);

/**
 * @brief Set number of blinks per cycle
 *
 * blinks = 0   -> LED always OFF
 * blinks = 1-9 -> Blink N times, pause, repeat
 * blinks >=10  -> LED always ON
 *
 * Safe to call at runtime from any task.
 */
void led_utils_set_blinks(uint8_t blinks);

/**
 * @brief Configure LED timings (in milliseconds)
 *
 * Safe to call at runtime.
 */
void led_utils_set_timings(uint32_t blink_on_ms,
                           uint32_t blink_off_ms,
                           uint32_t cycle_pause_ms);

#endif // LED_UTILS_H
