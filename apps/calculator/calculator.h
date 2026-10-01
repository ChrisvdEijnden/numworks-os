#pragma once
#include "../../kernel/kernel.h"
void calculator_init(void);
void calculator_redraw(void);
void calculator_handle_event(const kernel_event_t *ev);
/* History, oldest first (for tests) */
int         calculator_history_count(void);
const char *calculator_history_expr(int i);
const char *calculator_history_result(int i);
const char *calculator_input(void);
