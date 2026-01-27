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

#define SLEEP_TIME_MS   500

#define LED_NODE		DT_ALIAS(led_pin)

static void sampling_thread(void);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

K_THREAD_DEFINE(sampling_thread_id, 4096, sampling_thread, NULL, NULL, NULL, 10, 0, -1);

K_SEM_DEFINE(sample_ready_sem, 0, 1);

LOG_MODULE_REGISTER(main);

void sample_ready_isr(void *arg)
{
	ARG_UNUSED(arg);

	if (NRF_COMP->EVENTS_DOWN) {
		NRF_COMP->EVENTS_DOWN = 0;
		
		k_sem_give(&sample_ready_sem);
	}
}

void timer_overrun_isr(void *arg)
{
	ARG_UNUSED(arg);

	if (NRF_TIMER20->EVENTS_COMPARE[1]) {
		NRF_TIMER20->EVENTS_COMPARE[1] = 0;
		NRF_TIMER20->TASKS_STOP = 1;
		
		LOG_ERR("Timer overrun!");
	}
}

static int sampling_init(void)
{
	NRF_COMP->REFSEL   = (COMP_REFSEL_REFSEL_VDD << COMP_REFSEL_REFSEL_Pos);
    NRF_COMP->TH       = (5 << COMP_TH_THDOWN_Pos) | (60 << COMP_TH_THUP_Pos);
    NRF_COMP->MODE     = (COMP_MODE_MAIN_SE << COMP_MODE_MAIN_Pos) | (COMP_MODE_SP_High << COMP_MODE_SP_Pos);
    NRF_COMP->ISOURCE  = (COMP_ISOURCE_ISOURCE_Ien10uA << COMP_ISOURCE_ISOURCE_Pos);
    NRF_COMP->INTENSET = COMP_INTEN_DOWN_Msk;
    NRF_COMP->SHORTS   = COMP_SHORTS_DOWN_STOP_Msk;

	NRF_TIMER20->PRESCALER   = 0;
    NRF_TIMER20->BITMODE     = (TIMER_BITMODE_BITMODE_16Bit << TIMER_BITMODE_BITMODE_Pos);
    NRF_TIMER20->CC[1]       = 1000*16;
    NRF_TIMER20->SHORTS      = (TIMER_SHORTS_COMPARE1_CLEAR_Msk | TIMER_SHORTS_COMPARE1_STOP_Msk);
    NRF_TIMER20->INTENSET    = TIMER_INTENSET_COMPARE1_Msk;
    NRF_TIMER20->TASKS_CLEAR = 1;

	NRF_COMP->PUBLISH_UP = (0 << COMP_PUBLISH_UP_CHIDX_Pos) | COMP_PUBLISH_UP_EN_Msk;
	NRF_TIMER20->SUBSCRIBE_START = (0 << TIMER_SUBSCRIBE_START_CHIDX_Pos) | TIMER_SUBSCRIBE_START_EN_Msk;
	NRF_DPPIC20->CHENSET = DPPIC_CHENSET_CH0_Msk;

	NRF_COMP->PUBLISH_DOWN = (1 << COMP_PUBLISH_DOWN_CHIDX_Pos) | COMP_PUBLISH_DOWN_EN_Msk;
	NRF_TIMER20->SUBSCRIBE_CAPTURE[0] = (1 << TIMER_SUBSCRIBE_CAPTURE_CHIDX_Pos) | TIMER_SUBSCRIBE_CAPTURE_EN_Msk;
	NRF_TIMER20->SUBSCRIBE_STOP = (1 << TIMER_SUBSCRIBE_STOP_CHIDX_Pos) | TIMER_SUBSCRIBE_STOP_EN_Msk;
	NRF_DPPIC20->CHENSET = DPPIC_CHENSET_CH1_Msk;
	
	IRQ_CONNECT(COMP_IRQn, 3, sample_ready_isr, NULL, 0);
	irq_enable(COMP_IRQn);

	IRQ_CONNECT(TIMER20_IRQn, 3, timer_overrun_isr, NULL, 0);
	irq_enable(TIMER20_IRQn);

	return 0;
}

static void sampling_thread(void)
{
	uint32_t value;

	LOG_INF("Start sampling thread");

	while (true) {
		LOG_INF("Sampling...");

		NRF_POWER->TASKS_CONSTLAT = 1;

		NRF_TIMER20->TASKS_CLEAR = 1;

		NRF_COMP->PSEL = (1 << COMP_PSEL_PORT_Pos) | (4 << COMP_PSEL_PIN_Pos);
		NRF_COMP->ENABLE = (COMP_ENABLE_ENABLE_Enabled << COMP_ENABLE_ENABLE_Pos);
		NRF_COMP->TASKS_START = 1;

		k_sem_take(&sample_ready_sem, K_FOREVER);
		
		NRF_POWER->TASKS_CONSTLAT = 0;

		value = NRF_TIMER20->CC[0];

		LOG_INF("Done, value=%d", value);

		k_sleep(K_MSEC(1000));
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

	err = sampling_init();

	if (err) {
		LOG_ERR("Failed to init sampling thread, err %d", err);
		return 3;
	}

	k_thread_start(sampling_thread_id);

	LOG_INF("Main loop");

	while (true) {
		err = gpio_pin_toggle_dt(&led);
		
		if (err < 0) {
			return 3;
		}

		k_msleep(SLEEP_TIME_MS);
	}
}
