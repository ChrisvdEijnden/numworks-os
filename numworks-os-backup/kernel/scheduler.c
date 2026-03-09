/* ================================================================
 * NumWorks OS — Cooperative Scheduler
 * File: kernel/scheduler.c
 *
 * Simple round-robin with priority:
 *   - Higher priority tasks always run before lower ones
 *   - Tasks call scheduler_yield() to give up CPU
 *   - No context switching overhead — just function calls
 *
 * Code size target: < 1.5 KB
 * ================================================================ */
#include "scheduler.h"
#include <string.h>

static task_t  s_tasks[MAX_TASKS];
static uint8_t s_ntasks = 0;
static uint8_t s_current = 0;
static volatile bool s_yield = false;

void scheduler_init(void) {
    memset(s_tasks, 0, sizeof(s_tasks));
    s_ntasks = 0;
}

void scheduler_add_task(const char *name, task_fn_t fn, uint8_t prio) {
    if (s_ntasks >= MAX_TASKS) return;
    s_tasks[s_ntasks].name    = name;
    s_tasks[s_ntasks].fn      = fn;
    s_tasks[s_ntasks].prio    = prio;
    s_tasks[s_ntasks].waiting = 0;
    s_ntasks++;
}

/* Run highest-priority non-waiting task */
void scheduler_run_next(void) {
    int8_t best = -1;
    uint8_t best_prio = 0;

    for (uint8_t i = 0; i < s_ntasks; i++) {
        uint8_t idx = (s_current + 1 + i) % s_ntasks;
        if (!s_tasks[idx].waiting && s_tasks[idx].prio >= best_prio) {
            best      = (int8_t)idx;
            best_prio = s_tasks[idx].prio;
        }
    }
    if (best < 0) return;

    s_current = (uint8_t)best;
    s_yield   = false;
    s_tasks[s_current].run_count++;
    s_tasks[s_current].fn();
}

void scheduler_yield(void) {
    s_yield = true;
}

bool scheduler_all_waiting(void) {
    for (uint8_t i = 0; i < s_ntasks; i++) {
        if (!s_tasks[i].waiting) return false;
    }
    return true;
}

void scheduler_tick(void) {
    /* Wake all waiting tasks on each tick (1 kHz) */
    for (uint8_t i = 0; i < s_ntasks; i++) {
        if (s_tasks[i].waiting) s_tasks[i].waiting--;
    }
}
