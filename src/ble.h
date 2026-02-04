/**
 * @file    ble.h
 * @author  Matthijs Bakker
 * @date    2026-02-03
 * @brief   BLE Keyboard HID implementation
 */

#include <stdint.h>
#include <stdbool.h>

/**
 * Keyboard key index.
 * 
 * The positions of these bits must match their positions in the HID report (see ble.c).
 */
typedef enum {
    BLE_HID_KEY_MUTE        = (1 << 0),
    BLE_HID_KEY_PLAYPAUSE   = (1 << 1),
    BLE_HID_KEY_VOLUME_UP   = (1 << 2),
    BLE_HID_KEY_VOLUME_DOWN = (1 << 3),
} ble_hid_key_t;

/**
 * Message which indicates which key was most recently pressed/released,
 * and the mask of all currently pressed keys.
 */
typedef struct {
    int button;
    bool button_pressed;
    ble_hid_key_t pressed_mask;
} ble_key_input_t;

/**
 * Initialize the Bluetooth subsystem + keyboard HIDS.
 * 
 * @returns 0 on success,
 *          >0 on failure
 */
int ble_init(void);

/**
 * Send the updated key input to all connected clients.
 * 
 * @param input An input object with the latest pressed/released
 *              key and a map of all currently pressed keys.
 */
void ble_send_key_input(const ble_key_input_t *input);
