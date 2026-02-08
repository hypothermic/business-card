/**
 * @file    led.h
 * @author  Matthijs Bakker
 * @date    2026-02-07
 * @brief   LEDs control
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define LED_SHORT_BLINK_DURATION    K_MSEC(100)
#define LED_LONG_BLINK_DURATION     K_MSEC(2000)

typedef enum {
	LED_INDEX_RED,
	LED_INDEX_GREEN,
	LED_INDEX_BLUE,

    __LED_INDEX_MAX,
} led_index_t;

typedef struct {
	const struct gpio_dt_spec gpio_spec;
    struct k_msgq *msgq;
	led_index_t index;
} led_data_t;

int led_blink(led_index_t index, k_timeout_t timeout);
