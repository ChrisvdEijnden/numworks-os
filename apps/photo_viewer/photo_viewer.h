#pragma once
#include "../../kernel/kernel.h"
void photo_viewer_init(void);
void photo_viewer_redraw(void);
void photo_viewer_handle_event(const kernel_event_t *ev);
