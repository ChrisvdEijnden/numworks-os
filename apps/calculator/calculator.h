#pragma once
#include "../../kernel/kernel.h"
void calculator_init(void);
void calculator_redraw(void);
void calculator_handle_event(const kernel_event_t *ev);
