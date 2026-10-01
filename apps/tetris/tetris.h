#pragma once
#include "../../kernel/kernel.h"
void tetris_init(void);
void tetris_redraw(void);
void tetris_handle_event(const kernel_event_t *ev);
void tetris_tick(void);   /* gravity; called every loop while shown */
