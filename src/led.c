/**
 * @file    led.c
 * @author  Matthijs Bakker
 * @date    2026-02-07
 * @brief   LEDs control
 */

#include "led.h"

#include <zephyr/logging/log.h>

#define LED_DEFINE(name, _index) \
    K_MSGQ_DEFINE(name ## _led_msgq, sizeof(k_timeout_t), 10, 1); \
    static const led_data_t name ## _led_data = { \
        .gpio_spec = GPIO_DT_SPEC_GET(DT_ALIAS(name ## _led), gpios), \
        .index = _index, \
        .msgq = & name ## _led_msgq, \
    }; \
    K_THREAD_DEFINE(name ## _led_thread_id, 4096, led_thread, &name ## _led_data, NULL, NULL, 12, 0, 1);

static const led_data_t *led_data[__LED_INDEX_MAX];

LOG_MODULE_REGISTER(led);

static void led_thread(void *data_p)
{
    const led_data_t *data = data_p;
    k_timeout_t timeout, blink_rate = K_FOREVER;
    int err;

    led_data[data->index] = data;

    if (!gpio_is_ready_dt(&data->gpio_spec)) {
        LOG_ERR("LED %d thread: gpio not ready", data->index);
        k_sleep(K_FOREVER);
    }

    err = gpio_pin_configure_dt(&data->gpio_spec, GPIO_OUTPUT_INACTIVE);

    if (err < 0) {
        LOG_ERR("Failed to configure led %d gpio, err %d", data->index, err);
        k_sleep(K_FOREVER);
    }

    while (true) {
        err = k_msgq_get(data->msgq, &timeout, blink_rate);

        if (err && err != -EAGAIN) {
            LOG_ERR("Failed to get from msgq, err %d", err);
            continue;
        }

        if (err != -EAGAIN) {
            switch (timeout.ticks) {
                case 0:
                    blink_rate = K_FOREVER;
                    continue;
                case SYS_FOREVER_MS:
                    blink_rate = timeout = LED_SHORT_BLINK_DURATION;
                    break;
                default:
                    blink_rate = K_FOREVER;
                    break;
            }
        }

        gpio_pin_set_dt(&data->gpio_spec, 1);

        k_sleep(timeout);
        
        gpio_pin_set_dt(&data->gpio_spec, 0);
    }
}

int led_blink(led_index_t index, k_timeout_t timeout)
{
    const led_data_t *data;
    int i;

    for (i = 0; i < ARRAY_SIZE(led_data); ++i) {
        data = led_data[i];

        if (data->index == index) {
            k_msgq_put(data->msgq, &timeout, K_NO_WAIT);
            return 0;
        }
    }

    LOG_ERR("No LED found with index %d", index);

    return 1;
}

LED_DEFINE(red,   LED_INDEX_RED);
LED_DEFINE(green, LED_INDEX_GREEN);
LED_DEFINE(blue,  LED_INDEX_BLUE);
