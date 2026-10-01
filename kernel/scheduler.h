#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../include/config.h"

#define TASK_PRIO_IDLE   0
#define TASK_PRIO_LOW    1
#define TASK_PRIO_NORMAL 2
#define TASK_PRIO_HIGH   3

typedef void (*task_fn_t)(void);

typedef struct {
    task_fn_t   fn;
    const char *name;
    uint8_t     prio;
    volatile uint8_t waiting;   /* ticks left to sleep; decremented by SysTick */
    uint32_t    run_count;
} task_t;

void  scheduler_init(void);
void  scheduler_add_task(const char *name, task_fn_t fn, uint8_t prio);
void  scheduler_run_next(void);
void  scheduler_tick(void);
void  scheduler_sleep(uint8_t ticks);         /* current task skips `ticks` SysTicks */
bool  scheduler_ready_above(uint8_t prio);    /* a task above `prio` can run now */
