/**
 * @file    usbms.h
 * @author  Matthijs Bakker
 * @date    2026-02-04
 * @brief   USB Mass Storage RAM Disk
 */

#include <stdint.h>

/**
 * Initialize the USB device + ramdisk.
 * 
 * @returns 0 on success,
 *          >0 on failure
 */
int usbms_init(void);
