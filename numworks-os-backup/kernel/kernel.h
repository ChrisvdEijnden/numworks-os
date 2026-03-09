#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum { KERNEL_BOOT, KERNEL_RUNNING, KERNEL_FAULT } kernel_state_t;
typedef enum { APP_SHELL, APP_FILEMANAGER, APP_PYTHON } app_state_t;

typedef struct {
    uint8_t     key;
    uint8_t     action;  /* 0=press, 1=release, 2=repeat */
    uint16_t    _pad;
} kernel_event_t;

typedef struct {
    kernel_state_t  state;
    app_state_t     app_state;
    uint32_t        idle_count;
} kernel_t;

void  kernel_main(void);
void  kernel_post_event(uint8_t key, uint8_t action);
bool  kernel_event_get(kernel_event_t *out);
void  kernel_set_app(app_state_t app);

/* Task functions */
void task_idle(void);
void task_input(void);
void task_display(void);
void task_shell(void);
