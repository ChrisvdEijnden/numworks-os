/* ================================================================
 * NumWorks OS — Kernel (Extended for N0120 Custom Firmware)
 * File: kernel/kernel.c
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
#include "../apps/home/home.h"
#include "../apps/calculator/calculator.h"
#include "../apps/functions/functions.h"
#include "../apps/equations/equations.h"
#include "../apps/python_app/python_app.h"
#include "../apps/tetris/tetris.h"
#include "../apps/docs_app/docs_app.h"
#include "../apps/settings/settings.h"
#include "../apps/photo_viewer/photo_viewer.h"
#include "../apps/text_editor/text_editor.h"
#include "../include/string.h"

static kernel_t  g_kernel;
static volatile bool s_redraw_pending = false;
extern volatile uint32_t g_tick_ms;

void SysTick_Handler(void) {
    g_tick_ms++;
    scheduler_tick();
}

void kernel_init(void) {
    mem_init();
    scheduler_init();
    g_kernel.state      = KERNEL_BOOT;
    g_kernel.app_state  = APP_HOME;
    g_kernel.idle_count = 0;
}

void kernel_run(void) {
    scheduler_add_task("idle",    task_idle,    TASK_PRIO_IDLE);
    scheduler_add_task("input",   task_input,   TASK_PRIO_NORMAL);
    scheduler_add_task("display", task_display, TASK_PRIO_NORMAL);
    scheduler_add_task("app",     task_shell,   TASK_PRIO_NORMAL);

    g_kernel.state = KERNEL_RUNNING;

    /* Each task sleeps one tick after it runs; when none is ready the
     * idle task puts the CPU to sleep until the next interrupt. */
    while (1) {
        scheduler_run_next();
    }
}

/* ── Built-in tasks ──────────────────────────────────────────── */
void task_idle(void) {
    g_kernel.idle_count++;
    /* Sleep until the next interrupt (SysTick at the latest). Interrupts
     * are masked around the check so a tick that wakes a task between
     * the check and WFI isn't missed: WFI still wakes on a pending
     * interrupt while PRIMASK is set, and it is taken right after. */
    __asm volatile("cpsid i" ::: "memory");
    if (!scheduler_ready_above(TASK_PRIO_IDLE)) __asm volatile("wfi");
    __asm volatile("cpsie i" ::: "memory");
    scheduler_yield();
}

void task_input(void) {
    key_event_t ev;
    while (keyboard_poll(&ev)) {
        kernel_post_event(ev.key, ev.action);
    }
    scheduler_sleep(1);
}

void task_display(void) {
    display_update();
    scheduler_sleep(1);
}

/* App dispatch */
void task_shell(void) {
    kernel_event_t ev;
    while (kernel_event_get(&ev)) {
        switch (g_kernel.app_state) {
            case APP_HOME:         home_handle_event(&ev);          break;
            case APP_CALCULATOR:   calculator_handle_event(&ev);    break;
            case APP_FUNCTIONS:    functions_handle_event(&ev);     break;
            case APP_EQUATIONS:    equations_handle_event(&ev);     break;
            case APP_PYTHON:       python_app_handle_event(&ev);    break;
            case APP_SHELL:        shell_handle_event(&ev);         break;
            case APP_FILEMANAGER:  fm_handle_event(&ev);            break;
            case APP_TETRIS:       tetris_handle_event(&ev);        break;
            case APP_DOCS:         docs_handle_event(&ev);          break;
            case APP_SETTINGS:     settings_handle_event(&ev);      break;
            case APP_PHOTO_VIEWER: photo_viewer_handle_event(&ev);  break;
            case APP_TEXT_EDITOR:  text_editor_handle_event(&ev);   break;
            default: break;
        }
    }
    if (s_redraw_pending) {
        s_redraw_pending = false;
        kernel_set_app(g_kernel.app_state);
    }
    usb_cdc_process();
    scheduler_sleep(1);
}

/* ── Event queue ─────────────────────────────────────────────── */
#define EVT_QUEUE_SIZE 16
static kernel_event_t s_evq[EVT_QUEUE_SIZE];
static uint8_t s_evq_head = 0, s_evq_tail = 0;

void kernel_post_event(uint8_t key, uint8_t action) {
    uint8_t next = (s_evq_tail + 1) % EVT_QUEUE_SIZE;
    if (next == s_evq_head) return;
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

void kernel_request_redraw(void) {
    s_redraw_pending = true;
}

app_state_t kernel_get_app(void) {
    return g_kernel.app_state;
}

void kernel_set_app(app_state_t app) {
    g_kernel.app_state = app;
    /* Trigger redraw for the new app */
    switch (app) {
        case APP_HOME:         home_redraw();              break;
        case APP_CALCULATOR:   calculator_redraw();        break;
        case APP_FUNCTIONS:    functions_redraw();         break;
        case APP_EQUATIONS:    equations_redraw();         break;
        case APP_PYTHON:       python_app_redraw();        break;
        case APP_SHELL:        shell_redraw();             break;
        case APP_FILEMANAGER:  fm_redraw();                break;
        case APP_TETRIS:       tetris_redraw();            break;
        case APP_DOCS:         docs_redraw();              break;
        case APP_SETTINGS:     settings_redraw();         break;
        case APP_PHOTO_VIEWER: photo_viewer_redraw();      break;
        case APP_TEXT_EDITOR:  text_editor_redraw();       break;
        default: break;
    }
}
