/**
 * @file    ble.h
 * @author  Matthijs Bakker
 * @date    2026-02-03
 * @brief   
 */

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int button;
    bool button_pressed;
    uint32_t pressed_mask;
} ble_key_input_t;

void ble_send_key_input(const ble_key_input_t *input);

int ble_init(void);
