#pragma once
#include <stdint.h>
void     usb_cdc_init(void);
void     usb_cdc_process(void);
int      usb_cdc_write(const void *buf, int len);
int      usb_cdc_read(void *buf, int maxlen);
int      usb_cdc_available(void);
