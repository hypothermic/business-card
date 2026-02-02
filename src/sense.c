/**
 * @file    sense.c
 * @author  Matthijs Bakker
 * @date    2026-02-02
 * @brief   
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <nrf.h>

K_SEM_DEFINE(sample_ready_sem, 0, 1);

LOG_MODULE_REGISTER(sense);

static void sample_ready_isr(void *arg)
{
	ARG_UNUSED(arg);

	if (NRF_COMP->EVENTS_CROSS) {
		NRF_COMP->EVENTS_CROSS = 0;

		LOG_DBG("Cross");

		k_sem_give(&sample_ready_sem);
	}
}

static void timer_overrun_isr(void *arg)
{
	ARG_UNUSED(arg);

	if (NRF_TIMER1->EVENTS_COMPARE[1]) {
		NRF_TIMER1->EVENTS_COMPARE[1] = 0;
		NRF_TIMER1->TASKS_STOP = 1;
		
		LOG_DBG("Timer overrun!");
	}
}

int sense_pin(int pin, uint32_t *value)
{
    int err;

    NRF_TIMER1->TASKS_STOP = 1;
	NRF_TIMER1->TASKS_CLEAR = 1;
	
	NRF_DPPIC->TASKS_CHG[0].EN = 1;

	NRF_COMP->PSEL = (pin << COMP_PSEL_PSEL_Pos);
	NRF_COMP->ENABLE = (COMP_ENABLE_ENABLE_Enabled << COMP_ENABLE_ENABLE_Pos);
	NRF_COMP->TASKS_START = 1;

	// Await two COMP crossings before the wave period is in timer capture compare register

	err = k_sem_take(&sample_ready_sem, K_MSEC(2));

    if (err) {
        LOG_ERR("Failed to capture first crossing, err %d", err);
        return 2;
    }

	err = k_sem_take(&sample_ready_sem, K_MSEC(2));

    if (err) {
        LOG_ERR("Failed to capture second crossing, err %d", err);
        return 3;
    }

	*value = NRF_TIMER1->CC[0];

    return 0;
}

int sense_init(void)
{
	NRF_COMP->REFSEL   = (COMP_REFSEL_REFSEL_VDD << COMP_REFSEL_REFSEL_Pos);
    NRF_COMP->TH       = (5 << COMP_TH_THDOWN_Pos) | (60 << COMP_TH_THUP_Pos);
    NRF_COMP->MODE     = (COMP_MODE_MAIN_SE << COMP_MODE_MAIN_Pos) | (COMP_MODE_SP_High << COMP_MODE_SP_Pos);
    NRF_COMP->ISOURCE  = (COMP_ISOURCE_ISOURCE_Ien10mA << COMP_ISOURCE_ISOURCE_Pos);
    NRF_COMP->INTENSET = COMP_INTEN_CROSS_Msk;

	NRF_TIMER1->PRESCALER   = 0;
    NRF_TIMER1->BITMODE     = (TIMER_BITMODE_BITMODE_16Bit << TIMER_BITMODE_BITMODE_Pos);
    NRF_TIMER1->CC[1]       = 1000*16;
    NRF_TIMER1->SHORTS      = (TIMER_SHORTS_COMPARE1_CLEAR_Msk | TIMER_SHORTS_COMPARE1_STOP_Msk);
    NRF_TIMER1->INTENSET    = TIMER_INTENSET_COMPARE1_Msk;
    NRF_TIMER1->TASKS_CLEAR = 1;

	// Channel group 0 is for the initial V_{in} crossing event
	// Channel group 1 is for the termination crossing event

	NRF_COMP->PUBLISH_CROSS  = (0 << COMP_PUBLISH_CROSS_CHIDX_Pos)  | COMP_PUBLISH_CROSS_EN_Msk;
	NRF_COMP->PUBLISH_UP     = (1 << COMP_PUBLISH_UP_CHIDX_Pos)     | COMP_PUBLISH_UP_EN_Msk;
	NRF_COMP->PUBLISH_DOWN   = (1 << COMP_PUBLISH_DOWN_CHIDX_Pos)   | COMP_PUBLISH_DOWN_EN_Msk;
	NRF_COMP->SUBSCRIBE_STOP = (1 << COMP_SUBSCRIBE_STOP_CHIDX_Pos) | COMP_SUBSCRIBE_STOP_EN_Msk;

	NRF_TIMER1->SUBSCRIBE_START      = (0 << TIMER_SUBSCRIBE_START_CHIDX_Pos) | TIMER_SUBSCRIBE_START_EN_Msk;
	NRF_TIMER1->SUBSCRIBE_CAPTURE[0] = (1 << TIMER_SUBSCRIBE_CAPTURE_CHIDX_Pos) | TIMER_SUBSCRIBE_CAPTURE_EN_Msk;
	NRF_TIMER1->SUBSCRIBE_STOP       = (1 << TIMER_SUBSCRIBE_STOP_CHIDX_Pos) | TIMER_SUBSCRIBE_STOP_EN_Msk;

	NRF_DPPIC->CHG[0] = (DPPIC_CHG_CH0_Included << DPPIC_CHG_CH0_Pos);
	NRF_DPPIC->CHG[1] = (DPPIC_CHG_CH1_Included << DPPIC_CHG_CH1_Pos);

	NRF_DPPIC->SUBSCRIBE_CHG[0].DIS = (0 << DPPIC_SUBSCRIBE_CHG_DIS_CHIDX_Pos) | DPPIC_SUBSCRIBE_CHG_DIS_EN_Msk;
	NRF_DPPIC->SUBSCRIBE_CHG[1].EN  = (0 << DPPIC_SUBSCRIBE_CHG_EN_CHIDX_Pos)  | DPPIC_SUBSCRIBE_CHG_EN_EN_Msk;
	NRF_DPPIC->SUBSCRIBE_CHG[1].DIS = (1 << DPPIC_SUBSCRIBE_CHG_DIS_CHIDX_Pos) | DPPIC_SUBSCRIBE_CHG_DIS_EN_Msk;

	IRQ_CONNECT(COMP_LPCOMP_IRQn, 3, sample_ready_isr, NULL, 0);
	irq_enable(COMP_LPCOMP_IRQn);

	IRQ_CONNECT(TIMER1_IRQn, 3, timer_overrun_isr, NULL, 0);
	irq_enable(TIMER1_IRQn);

	return 0;
}
