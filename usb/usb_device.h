#pragma once
#include <stdbool.h>
#include <stdint.h>

/* USB device: the OTG_FS core as a CDC-ACM serial port (usb/usb_device.c).
 * Bytes move through the ring buffers in usb/usb_cdc.c. */
void usb_device_init(void);
/* From the main loop: connects/disconnects with VBUS, restarts
 * reception when the receive buffer has room, starts sending */
void usb_device_poll(void);
bool usb_device_configured(void);   /* enumerated by a host, port set up */
/* The interrupt handler's body (OTG_FS_IRQHandler calls it) */
void usb_device_irq(void);
