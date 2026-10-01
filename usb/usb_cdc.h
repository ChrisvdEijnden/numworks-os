#pragma once
#include <stdint.h>
void     usb_cdc_init(void);
void     usb_cdc_process(void);   /* run the file transfer protocol */
int      usb_cdc_write(const void *buf, int len);
int      usb_cdc_read(void *buf, int maxlen);
int      usb_cdc_available(void);
int      usb_cdc_tx_free(void);

/* For the USB device stack: bytes received from / to be sent to the PC */
int      usb_cdc_rx_push(const void *buf, int len);   /* returns bytes taken */
int      usb_cdc_tx_pop(void *buf, int maxlen);       /* returns bytes given */
