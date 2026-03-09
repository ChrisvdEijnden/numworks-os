#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#define TASK_PRIO_IDLE   0
#define TASK_PRIO_LOW    1
#define TASK_PRIO_NORMAL 2
#define TASK_PRIO_HIGH   3

typedef void (*task_fn_t)(void);

typedef struct {
    task_fn_t   fn;
    const char *name;
    uint8_t     prio;
    uint8_t     waiting;
    uint32_t    run_count;
} task_t;

void  scheduler_init(void);
void  scheduler_add_task(const char *name, task_fn_t fn, uint8_t prio);
void  scheduler_run_next(void);
void  scheduler_tick(void);
void  scheduler_yield(void);
bool  scheduler_all_waiting(void);
