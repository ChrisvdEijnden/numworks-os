#pragma once
/* The home screen's app icons (ui/icons.c, made by tools/icongen.py):
 * 55 x 56 pixels, run-length coded RGB565, drawn with ui_icon() */
#include <stdint.h>

#define ICON_W 55
#define ICON_H 56

typedef struct {
    uint16_t runs;              /* (count, colour) pairs */
    const uint16_t *rle;
} icon_t;

extern const icon_t icon_calculation, icon_functions, icon_equations, icon_statistics,
                    icon_python, icon_files, icon_editor, icon_shell, icon_games,
                    icon_help, icon_settings;

void ui_icon(const icon_t *icon, int16_t x, int16_t y);
