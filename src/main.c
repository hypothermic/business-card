/**
 * @file    main.c
 * @author  Matthijs Bakker
 * @date    2026-01-27
 * @brief   Application entry point
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <nrf.h>

#include "sense.h"

#define LED_NODE		DT_ALIAS(led_pin)

#define CALIBRATION_RUNS          10
#define CALIBRATION_THRESHOLD     1.2
#define DEBOUNCING_THRESHOLD      5

typedef struct {
	struct gpio_dt_spec led_gpio;
	int analog_input;

    uint32_t threshold;
    uint8_t debouncing_streak;
    bool pressed;
} touchpad_data_t;

static void sampling_thread(void);

static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

static touchpad_data_t touchpad_data[] = {
	{
		.led_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
		.analog_input = COMP_PSEL_PSEL_AnalogInput0,
	},
	{
		.led_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios),
		.analog_input = COMP_PSEL_PSEL_AnalogInput1,
	},
	{
		.led_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
		.analog_input = COMP_PSEL_PSEL_AnalogInput2,
	},
};

K_THREAD_DEFINE(sampling_thread_id, 4096, sampling_thread, NULL, NULL, NULL, 10, 0, -1);

LOG_MODULE_REGISTER(main);

static void sampling_thread(void)
{
	uint32_t delta_time;
	bool touch_detected;
	int calibration_rounds_remaining = CALIBRATION_RUNS;
	int i;
	int err;

	LOG_INF("Start sampling thread");

	while (true) {
		k_sleep(K_MSEC(6));

		for (i = 0; i < ARRAY_SIZE(touchpad_data); ++i) {
			err = sense_pin(touchpad_data[i].analog_input, &delta_time);

			if (err) {
				LOG_ERR("Failed to sample analog pin, err %d", err);
				continue;
			}

			if (calibration_rounds_remaining) {
				touchpad_data[i].threshold += delta_time;
			} else {
				touch_detected = (delta_time > touchpad_data[i].threshold);

				if (touchpad_data[i].pressed != touch_detected) {
					LOG_INF("Debounce %s %d", touch_detected ? "up" : "down", touchpad_data[i].debouncing_streak);
					
					if (++touchpad_data[i].debouncing_streak > DEBOUNCING_THRESHOLD) {
						touchpad_data[i].debouncing_streak = 0;
						touchpad_data[i].pressed = touch_detected;
						gpio_pin_set_dt(&touchpad_data[i].led_gpio, touch_detected);
					}
				} else {
					touchpad_data[i].debouncing_streak = 0;
				}
			}
		}

		if (calibration_rounds_remaining > 0) {
			--calibration_rounds_remaining;

			// We just finished the last calibration round.
			// Calculate the arithmetic mean of the measured values.
			if (!calibration_rounds_remaining) {
				
				for (i = 0; i < ARRAY_SIZE(touchpad_data); ++i) {
					touchpad_data[i].threshold /= CALIBRATION_RUNS;
					touchpad_data[i].threshold *= CALIBRATION_THRESHOLD;
					LOG_INF("Threshold of touchpad %d set at %d", i, touchpad_data[i].threshold);
				}
			}
		}
	}
}

int main(void)
{
	int i, err;

	LOG_INF("Start main");

	if (!gpio_is_ready_dt(&status_led)) {
		return 1;
	}

	err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_ACTIVE);

	if (err < 0) {
		LOG_ERR("Failed to configure status LED gpio pin, err %d", err);
		return 2;
	}

	for (i = 0; i < ARRAY_SIZE(touchpad_data); ++i) {
		err = gpio_pin_configure_dt(&touchpad_data[i].led_gpio, GPIO_OUTPUT_INACTIVE);

		if (err < 0) {
			LOG_ERR("Failed to configure touchpad %d LED gpio pin, err %d", i, err);
			return 2;
		}
	}

	err = sense_init();

	if (err) {
		LOG_ERR("Failed to init sampling thread, err %d", err);
		return 3;
	}
	
	NRF_POWER->TASKS_CONSTLAT = 1;

	k_thread_start(sampling_thread_id);

	LOG_INF("Main loop");

	while (true) {
		gpio_pin_set_dt(&status_led, 0);
		k_msleep(2000);

		gpio_pin_set_dt(&status_led, 1);
		k_msleep(100);
	}
}
