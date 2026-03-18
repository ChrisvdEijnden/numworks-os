#pragma once
#include "../../kernel/kernel.h"
void text_editor_init(void);
void text_editor_redraw(void);
void text_editor_handle_event(const kernel_event_t *ev);
