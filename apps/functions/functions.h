#pragma once
#include "../../kernel/kernel.h"
void functions_init(void);
void functions_redraw(void);
void functions_handle_event(const kernel_event_t *ev);
/* For tests: analysis right of the cursor (kind: 0 zero, 1 min, 2 max,
 * 3 intersection), the footer text and the trace cursor */
bool        functions_analyse(int kind, double *x, int *other_fn);
const char *functions_info(void);
bool        functions_tracing(void);
double      functions_cursor_x(void);
