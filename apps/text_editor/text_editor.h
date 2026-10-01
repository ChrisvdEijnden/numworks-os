#pragma once
#include "../../kernel/kernel.h"
void text_editor_init(void);
void text_editor_redraw(void);
void text_editor_handle_event(const kernel_event_t *ev);

/* Load a file for editing. Returns false (and leaves the editor
 * untouched) if it can't be read or is larger than the editor holds. */
bool text_editor_open(const char *name);
