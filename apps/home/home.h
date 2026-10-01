#pragma once
#include "../../kernel/kernel.h"
void home_init(void);
void home_redraw(void);
void home_draw_status(void);   /* header: battery, warnings */
void home_handle_event(const kernel_event_t *ev);
