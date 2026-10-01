#pragma once
#include "../../kernel/kernel.h"

#define STATS_FILE "stats.csv"

void statistics_init(void);
void statistics_redraw(void);
void statistics_handle_event(const kernel_event_t *ev);

/* For tests */
int    statistics_rows(void);
double statistics_cell(int row, int col);     /* NAN if empty */
void   statistics_set_text(const char *text, unsigned len);
void   statistics_save(void);
