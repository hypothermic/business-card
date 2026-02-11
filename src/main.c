/**
 * @file    main.c
 * @author  Matthijs Bakker
 * @date    2026-01-27
 * @brief   Application entry point
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "ble.h"
#include "led.h"
#include "sense.h"
#include "usbms.h"

#define CALIBRATION_RUNS            10
#define CALIBRATION_THRESHOLD       1.75
#define DEBOUNCING_THRESHOLD        5
#define MAX_SAMPLE_RETRIES          5

typedef struct {
	int analog_input;
	ble_hid_key_t emulated_key;

    uint32_t threshold;
    uint8_t debouncing_streak;
    bool pressed;
} touchpad_data_t;

static touchpad_data_t touchpad_data[] = {
#if CONFIG_BOARD_MBC10
	{
		.analog_input = COMP_PSEL_PSEL_AnalogInput0,
		.emulated_key = BLE_HID_KEY_PLAYPAUSE,
	},
	{
		.analog_input = COMP_PSEL_PSEL_AnalogInput3,
		.emulated_key = BLE_HID_KEY_VOLUME_DOWN,
	},
	{
		.analog_input = COMP_PSEL_PSEL_AnalogInput1,
		.emulated_key = BLE_HID_KEY_VOLUME_UP,
	},
	{
		.analog_input = COMP_PSEL_PSEL_AnalogInput5,
		.emulated_key = BLE_HID_KEY_MUTE,
	},
#else
	{
		.analog_input = COMP_PSEL_PSEL_AnalogInput3,
		.emulated_key = BLE_HID_KEY_VOLUME_UP,
	},
	{
		.analog_input = COMP_PSEL_PSEL_AnalogInput0,
		.emulated_key = BLE_HID_KEY_VOLUME_DOWN,
	},
	{
		.analog_input = COMP_PSEL_PSEL_AnalogInput1,
		.emulated_key = BLE_HID_KEY_PLAYPAUSE,
	},
#endif
};

static void sampling_thread(void);

K_THREAD_DEFINE(sampling_thread_id, 4096, sampling_thread, NULL, NULL, NULL, 10, 0, -1);

LOG_MODULE_REGISTER(main);

static void touchpad_state_changed(int index, const touchpad_data_t *data)
{
	static ble_key_input_t input = {0};

	input.button = data->emulated_key;
	input.button_pressed = data->pressed;

	if (data->pressed) {
		input.pressed_mask |= data->emulated_key;
	} else {
		input.pressed_mask &= ~(data->emulated_key);
	}

	LOG_INF("State change %d %02x", input.button, input.pressed_mask);

	ble_send_key_input(&input);

	if (data->pressed) {
		led_blink(LED_INDEX_BLUE, LED_SHORT_BLINK_DURATION);
	}
}

static void sampling_thread(void)
{
	uint32_t delta_time;
	bool touch_detected;
	int calibration_rounds_remaining = CALIBRATION_RUNS;
	int i;
	int retry = 0;
	int err;

	LOG_INF("Start sampling thread");

	while (true) {
		NRF_POWER->TASKS_CONSTLAT = 0;

		k_sleep(K_MSEC(6));
		
		NRF_POWER->TASKS_CONSTLAT = 1;

		for (i = 0; i < ARRAY_SIZE(touchpad_data); ++i) {
			err = sense_pin(touchpad_data[i].analog_input, &delta_time);

			if (err) {
				if (retry++ < MAX_SAMPLE_RETRIES) {
					i -= 1;
					continue;
				}

				LOG_ERR("Failed to sample analog pin, err %d", err);
			}

			retry = 0;

			if (calibration_rounds_remaining) {
				touchpad_data[i].threshold += delta_time;
			} else {
				touch_detected = (delta_time > touchpad_data[i].threshold);

				if (touchpad_data[i].pressed != touch_detected) {
					LOG_DBG("Debounce %d %s %d", i, touch_detected ? "up" : "down", touchpad_data[i].debouncing_streak);
					
					if (++touchpad_data[i].debouncing_streak > DEBOUNCING_THRESHOLD) {
						touchpad_data[i].debouncing_streak = 0;
						touchpad_data[i].pressed = touch_detected;
						touchpad_state_changed(i, &touchpad_data[i]);
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
	int err;

	LOG_INF("Start main");

	err = usbms_init();

	if (err) {
		LOG_ERR("Failed to init USB MS, err %d", err);
	}

	err = ble_init();

	if (err) {
		LOG_ERR("Failed to init BLE, err %d", err);
	}

	err = sense_init();

	if (!err) {
		k_thread_start(sampling_thread_id);
	} else {
		LOG_ERR("Failed to init sampling thread, err %d", err);
	}
	
	LOG_INF("Startup complete");

	led_blink(LED_INDEX_GREEN, LED_NORMAL_BLINK_DURATION);
}
