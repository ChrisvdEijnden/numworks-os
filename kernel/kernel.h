/* ================================================================
 * NumWorks OS — Kernel (Extended for N0120 Custom Firmware)
 * ================================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    KERNEL_BOOT,
    KERNEL_RUNNING,
    KERNEL_FAULT
} kernel_state_t;

typedef enum {
    APP_HOME = 0,
    APP_CALCULATOR,
    APP_FUNCTIONS,
    APP_EQUATIONS,
    APP_PYTHON,
    APP_SHELL,
    APP_FILEMANAGER,
    APP_TETRIS,
    APP_DOCS,
    APP_SETTINGS,
    APP_TEXT_EDITOR,
    APP_COUNT
} app_state_t;

typedef struct {
    uint8_t  key;
    uint8_t  action;   /* 0=press (auto-repeat also arrives as press), 1=release */
    uint16_t _pad;
} kernel_event_t;

typedef struct {
    kernel_state_t state;
    app_state_t    app_state;
    uint32_t       idle_count;
} kernel_t;

void  kernel_init(void);
void  kernel_run(void);
void  kernel_post_event(uint8_t key, uint8_t action);
bool  kernel_event_get(kernel_event_t *out);
void  kernel_set_app(app_state_t app);
void  kernel_request_redraw(void);   /* repaint the current app on the next loop */
app_state_t kernel_get_app(void);

/* Built-in task prototypes */
void task_idle(void);
void task_input(void);
void task_display(void);
void task_shell(void);
