#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "led_utils.h"

typedef enum
{
    LED_STATE_IDLE,
    LED_STATE_ON,
    LED_STATE_OFF,
    LED_STATE_PAUSE
} led_state_t;

static esp_timer_handle_t led_timer;

// Timing configuration
static uint32_t blink_on_ms = LED_DEFAULT_BLINK_ON_MS;
static uint32_t blink_off_ms = LED_DEFAULT_BLINK_OFF_MS;
static uint32_t cycle_pause_ms = LED_DEFAULT_CYCLE_PAUSE_MS;

// Blink configuration
static volatile uint8_t blink_target = 0;

// Runtime state
static led_state_t state = LED_STATE_IDLE;
static uint8_t blink_count = 0;

// Spinlock for shared state
static portMUX_TYPE led_mux = portMUX_INITIALIZER_UNLOCKED;

static inline void led_on(void)
{
    gpio_set_level(LED_GPIO_PIN, LED_ACTIVE_LEVEL);
}

static inline void led_off(void)
{
    gpio_set_level(LED_GPIO_PIN, !LED_ACTIVE_LEVEL);
}

// Timer Callback

static void led_timer_cb(void *arg)
{
    portENTER_CRITICAL_ISR(&led_mux);

    uint8_t blinks = blink_target;

    // Always OFF
    if (blinks == 0)
    {
        led_off();
        state = LED_STATE_IDLE;
        portEXIT_CRITICAL_ISR(&led_mux);
        esp_timer_start_once(led_timer, 500000); // check again in 500ms
        return;
    }

    // Always ON
    if (blinks >= 10)
    {
        led_on();
        state = LED_STATE_IDLE;
        portEXIT_CRITICAL_ISR(&led_mux);
        esp_timer_start_once(led_timer, 500000);
        return;
    }

    switch (state)
    {

    case LED_STATE_IDLE:
        blink_count = 0;
        led_on();
        state = LED_STATE_ON;
        portEXIT_CRITICAL_ISR(&led_mux);
        esp_timer_start_once(led_timer, blink_on_ms * 1000);
        return;

    case LED_STATE_ON:
        led_off();
        state = LED_STATE_OFF;
        portEXIT_CRITICAL_ISR(&led_mux);
        esp_timer_start_once(led_timer, blink_off_ms * 1000);
        return;

    case LED_STATE_OFF:
        blink_count++;
        if (blink_count >= blinks)
        {
            state = LED_STATE_PAUSE;
            portEXIT_CRITICAL_ISR(&led_mux);
            esp_timer_start_once(led_timer, cycle_pause_ms * 1000);
        }
        else
        {
            led_on();
            state = LED_STATE_ON;
            portEXIT_CRITICAL_ISR(&led_mux);
            esp_timer_start_once(led_timer, blink_on_ms * 1000);
        }
        return;

    case LED_STATE_PAUSE:
        blink_count = 0;
        led_on();
        state = LED_STATE_ON;
        portEXIT_CRITICAL_ISR(&led_mux);
        esp_timer_start_once(led_timer, blink_on_ms * 1000);
        return;
    }

    portEXIT_CRITICAL_ISR(&led_mux);
}

// Public API
void led_utils_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    led_off();

    esp_timer_create_args_t timer_args = {
        .callback = led_timer_cb,
        .name = "led_timer"};

    esp_timer_create(&timer_args, &led_timer);

    // Start immediately
    esp_timer_start_once(led_timer, 1000);
}

void led_utils_set_blinks(uint8_t blinks)
{
    portENTER_CRITICAL(&led_mux);
    blink_target = blinks;
    portEXIT_CRITICAL(&led_mux);
}

void led_utils_set_timings(uint32_t on_ms, uint32_t off_ms, uint32_t pause_ms)
{
    portENTER_CRITICAL(&led_mux);
    blink_on_ms = on_ms;
    blink_off_ms = off_ms;
    cycle_pause_ms = pause_ms;
    portEXIT_CRITICAL(&led_mux);
}
