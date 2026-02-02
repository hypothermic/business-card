/**
 * @file    sense.h
 * @author  Matthijs Bakker
 * @date    2026-02-02
 * @brief   
 */

#include <stdint.h>
#include <stdbool.h>

/**
 * Blocking function to measure capacitance on the specified pin.
 * 
 * @param pin    x, where x is AINx (x in [0, 7])
 * @param value  location to store the measured value
 * 
 * @returns 0 on success,
 *          >0 on failure
 */
int sense_pin(int pin, uint32_t *value);

/**
 * Initialize the capacitive sensing system.
 * 
 * @returns 0 on success,
 *          >0 on failure
 */
int sense_init(void);
