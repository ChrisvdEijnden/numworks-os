/* ================================================================
 * NumWorks OS — Minimal Kernel
 * File: kernel/kernel.c
 *
 * Architecture: cooperative event-loop scheduler
 *   - No preemption overhead
 *   - Tasks yield voluntarily
 *   - Tiny RAM footprint: 1 KB per task stack
 *   - Four built-in tasks: idle, input, display, app
 *
 * Code size target: < 8 KB
 * ================================================================ */
#include "kernel.h"
#include "scheduler.h"
#include "memory.h"
#include "../hal/hal.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "../shell/shell.h"
#include "../ui/filemanager.h"
#include "../usb/usb_cdc.h"
#include "../fs/flashfs.h"
#include <string.h>

/* ── Kernel state ─────────────────────────────────────────────── */
static kernel_t g_kernel;

/* ── SysTick override (also in boot.c as weak) ──────────────── */
extern volatile uint32_t g_tick_ms;
void SysTick_Handler(void) {
    g_tick_ms++;
    scheduler_tick();
}

/* ================================================================
 * kernel_init — called from main() to set up kernel structures
 * ================================================================ */
void kernel_init(void) {
    mem_init();
    scheduler_init();
    g_kernel.state     = KERNEL_BOOT;
    g_kernel.app_state = APP_SHELL;
    g_kernel.idle_count = 0;
}

/* ================================================================
 * kernel_run — starts the cooperative event loop, never returns
 * ================================================================ */
void kernel_run(void) {
    /* Register cooperative tasks */
    scheduler_add_task("idle",    task_idle,    TASK_PRIO_IDLE);
    scheduler_add_task("input",   task_input,   TASK_PRIO_NORMAL);
    scheduler_add_task("display", task_display, TASK_PRIO_NORMAL);
    scheduler_add_task("shell",   task_shell,   TASK_PRIO_NORMAL);

    g_kernel.state = KERNEL_RUNNING;

    /* ── Main event loop ─────────────────────────────────────── */
    while (1) {
        scheduler_run_next();

        /* Enter sleep between events to save power */
        if (scheduler_all_waiting()) {
            __asm volatile("wfi");
        }
    }
}

/* ================================================================
 * kernel_main — legacy entry kept for reference; not used by main.c
 * ================================================================ */
void kernel_main(void) {
    /* Init subsystems in dependency order */
    mem_init();
    hal_init();
    display_init();
    keyboard_init();
    usb_cdc_init();
    flashfs_init();

    /* Show boot splash for 500 ms */
    display_splash();

    /* Register cooperative tasks */
    scheduler_init();
    scheduler_add_task("idle",    task_idle,    TASK_PRIO_IDLE);
    scheduler_add_task("input",   task_input,   TASK_PRIO_NORMAL);
    scheduler_add_task("display", task_display, TASK_PRIO_NORMAL);
    scheduler_add_task("shell",   task_shell,   TASK_PRIO_NORMAL);

    g_kernel.state     = KERNEL_RUNNING;
    g_kernel.app_state = APP_SHELL;

    /* ── Main event loop ─────────────────────────────────────── */
    while (1) {
        scheduler_run_next();

        /* Enter sleep between events to save power */
        if (scheduler_all_waiting()) {
            __asm volatile("wfi");
        }
    }
}

/* ================================================================
 * Built-in tasks
 * ================================================================ */

/* Idle — lowest priority, just counts cycles */
void task_idle(void) {
    g_kernel.idle_count++;
    scheduler_yield();
}

/* Input — polls keyboard and queues events */
void task_input(void) {
    key_event_t ev;
    if (keyboard_poll(&ev)) {
        kernel_post_event(ev.key, ev.action);
    }
    scheduler_yield();
}

/* Display — flushes dirty regions to LCD */
void task_display(void) {
    display_flush();
    scheduler_yield();
}

/* Shell/App — processes queued events */
void task_shell(void) {
    kernel_event_t ev;
    while (kernel_event_get(&ev)) {
        switch (g_kernel.app_state) {
            case APP_SHELL:
                shell_handle_event(&ev);
                break;
            case APP_FILEMANAGER:
                fm_handle_event(&ev);
                break;
            default:
                break;
        }
    }
    /* Process USB transfers */
    usb_cdc_process();
    scheduler_yield();
}

/* ================================================================
 * Event queue (ring buffer — 16 events, zero malloc)
 * ================================================================ */
#define EVT_QUEUE_SIZE 16
static kernel_event_t s_evq[EVT_QUEUE_SIZE];
static uint8_t s_evq_head = 0, s_evq_tail = 0;

void kernel_post_event(uint8_t key, uint8_t action) {
    uint8_t next = (s_evq_tail + 1) % EVT_QUEUE_SIZE;
    if (next == s_evq_head) return;  /* Drop if full */
    s_evq[s_evq_tail].key    = key;
    s_evq[s_evq_tail].action = action;
    s_evq_tail = next;
}

bool kernel_event_get(kernel_event_t *out) {
    if (s_evq_head == s_evq_tail) return false;
    *out = s_evq[s_evq_head];
    s_evq_head = (s_evq_head + 1) % EVT_QUEUE_SIZE;
    return true;
}

/* Switch active application */
void kernel_set_app(app_state_t app) {
    g_kernel.app_state = app;
    if (app == APP_SHELL) {
        shell_redraw();
    } else if (app == APP_FILEMANAGER) {
        fm_redraw();
    }
}
