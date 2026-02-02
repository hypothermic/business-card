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

static void sampling_thread(void);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);

K_THREAD_DEFINE(sampling_thread_id, 4096, sampling_thread, NULL, NULL, NULL, 10, 0, -1);

LOG_MODULE_REGISTER(main);

static void sampling_thread(void)
{
	uint32_t value;
	int err;

	LOG_INF("Start sampling thread");

	while (true) {
		k_sleep(K_MSEC(50));

		LOG_INF("Sampling...");

		err = sense_pin(4, &value);

		if (err) {
			LOG_ERR("Failed to sample analog pin, err %d", err);
			continue;
		}

		LOG_INF("Sample: %d", value);

		gpio_pin_set_dt(&led2, (value > 200));
	}
}

int main(void)
{
	int err;

	LOG_INF("Start main");

	if (!gpio_is_ready_dt(&led)) {
		return 1;
	}

	err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

	if (err < 0) {
		return 2;
	}

	err = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_ACTIVE);

	if (err < 0) {
		return 2;
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
		gpio_pin_set_dt(&led, 0);
		k_msleep(2000);

		gpio_pin_set_dt(&led, 1);
		k_msleep(100);
	}
}
