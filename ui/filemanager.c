/* ================================================================
 * NumWorks OS — Graphical File Manager
 * File: ui/filemanager.c
 *
 * A lightweight file browser for the 320×240 display.
 *
 * Layout:
 *   ┌──────────────────────────────────────────┐
 *   │ rad            FILES                 [=] │  ← title bar
 *   │  ┌────────────────────────────────────┐  │
 *   │  │ hello.py                   1.2 KB  │  │  ← selected, shaded
 *   │  │ main.py                    0.4 KB  │  │
 *   │  │ ...                                │  │
 *   │  └────────────────────────────────────┘  │
 *   │ OK:edit  VAR:new  SHIFT:delete      1/8  │  ← hint bar
 *   └──────────────────────────────────────────┘
 *
 * Code size target: < 5 KB
 * ================================================================ */
#include "filemanager.h"
#include "lang.h"
#include "theme.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "../fs/flashfs.h"
#include "../kernel/kernel.h"
#include "../shell/shell.h"
#include "../apps/text_editor/text_editor.h"
#include <string.h>
#include <stdio.h>

#define LIST_X          UI_MARGIN
#define LIST_W          (LCD_WIDTH - 2 * UI_MARGIN)
#define LIST_Y          (UI_TITLE_H + 8)
#define ITEM_H          28
#define FOOTER_H        20
#define FOOTER_Y        (LCD_HEIGHT - FOOTER_H)
#define FM_VISIBLE_ROWS ((FOOTER_Y - LIST_Y - 4) / ITEM_H)      /* 6 */

typedef struct {
    char     name[FFS_NAME_LEN];
    uint32_t size;
} fm_item_t;

static fm_item_t s_items[FFS_MAX_FILES];
static int       s_nitem  = 0;
static int       s_cursor = 0;
static int       s_scroll = 0;
static bool      s_del_armed = false;   /* SHIFT pressed once: confirm delete */
static const char *s_msg = "";          /* one-shot footer message */

/* ── Load file list ──────────────────────────────────────────── */
/* Names starting with a dot (.settings) are the system's own: hidden */
static void list_cb(const ffs_entry_t *e, void *ctx) {
    int *n = (int *)ctx;
    if (e->name[0] != '.' && *n < FFS_MAX_FILES) {
        strncpy(s_items[*n].name, e->name, FFS_NAME_LEN-1);
        s_items[*n].name[FFS_NAME_LEN-1] = 0;
        s_items[*n].size = e->size;
        (*n)++;
    }
}

static void fm_load(void) {
    s_nitem = 0;
    flashfs_ls(list_cb, &s_nitem);
    /* The list may have shrunk: keep the cursor on a real entry */
    if (s_cursor >= s_nitem) s_cursor = s_nitem > 0 ? s_nitem - 1 : 0;
    if (s_scroll > s_cursor) s_scroll = s_cursor;
    if (s_cursor >= s_scroll + FM_VISIBLE_ROWS) s_scroll = s_cursor - FM_VISIBLE_ROWS + 1;
}

/* ── Drawing ─────────────────────────────────────────────────── */
/* A row of the list: the name, and the size on the right */
static void draw_row(int row, int item_idx, bool selected) {
    int16_t y = (int16_t)(LIST_Y + row * ITEM_H);
    if (item_idx >= s_nitem) {
        display_fill_rect(LIST_X, y, LIST_W, ITEM_H + 1, T_WALL);
        if (item_idx == 0)
            ui_text_center(LCD_WIDTH / 2, (int16_t)(y + 6), TR("Geen bestanden", "No files"), &font_large,
                           T_GRAY_VDARK, T_WALL);
        if (row > 0 && item_idx == s_nitem)        /* the last row's lower edge */
            display_hline(LIST_X, y, LIST_W, T_GRAY_BRIGHT);
        return;
    }
    char name[FFS_NAME_LEN + 2], size[16];
    int fit = (LIST_W - 20 - 70) / font_large.w;
    snprintf(name, sizeof name, "%s", s_items[item_idx].name);
    if ((int)strlen(name) > fit) strcpy(name + fit - 2, "..");
    uint32_t kb10 = (s_items[item_idx].size * 10) / 1024;
    snprintf(size, sizeof size, "%lu.%lu KB", (unsigned long)(kb10 / 10), (unsigned long)(kb10 % 10));
    ui_row(LIST_X, y, LIST_W, ITEM_H + 1, name, size, selected);
}

static void draw_header(void) {
    ui_title_bar(TR("Bestanden", "Files"));
    ui_scrollbar(LCD_WIDTH - 6, LIST_Y, FM_VISIBLE_ROWS * ITEM_H, s_scroll, FM_VISIBLE_ROWS, s_nitem);
}

/* The grey banner at the bottom: keys, a question or a message */
static void draw_footer(void) {
    display_fill_rect(0, FOOTER_Y, LCD_WIDTH, FOOTER_H, T_GRAY_BRIGHT);
    display_hline(0, FOOTER_Y, LCD_WIDTH, T_GRAY_MIDDLE);
    int16_t ty = FOOTER_Y + 4;
    if (s_del_armed && s_nitem > 0) {
        char line[48];
        snprintf(line, sizeof(line), TR("Wis %s? SHIFT=ja", "Delete %s? SHIFT=yes"), s_items[s_cursor].name);
        display_text(6, ty, line, &font_small, T_RED, T_GRAY_BRIGHT);
        return;
    }
    if (s_msg[0]) {
        display_text(6, ty, s_msg, &font_small, T_TEXT, T_GRAY_BRIGHT);
        return;
    }
    display_text(6, ty, TR("OK:bewerk  VAR:nieuw  SHIFT:wis", "OK:edit  VAR:new  SHIFT:delete"), &font_small,
                 T_GRAY_VDARK, T_GRAY_BRIGHT);
    char count[16];
    snprintf(count, sizeof count, "%d/%d", s_nitem ? s_cursor + 1 : 0, s_nitem);
    ui_text_right(LCD_WIDTH - 6, ty, count, &font_small, T_GRAY_VDARK, T_GRAY_BRIGHT);
}

static void draw_list(void) {
    for (int r = 0; r < FM_VISIBLE_ROWS; r++) draw_row(r, s_scroll + r, (s_scroll + r) == s_cursor);
}

void fm_redraw(void) {
    fm_load();
    ui_body(T_WALL);
    draw_header();
    draw_footer();
    draw_list();
}

/* ── Input ───────────────────────────────────────────────────── */
static void fm_delete(void) {
    if (s_nitem == 0) return;
    if (flashfs_delete(s_items[s_cursor].name) < 0) s_msg = TR("Wissen mislukt", "Delete failed");
    fm_redraw();
}

static void fm_open(void) {
    if (s_nitem == 0) return;
    if (text_editor_open(s_items[s_cursor].name)) {
        kernel_set_app(APP_TEXT_EDITOR);
    } else {
        s_msg = TR("Te groot voor de editor (max 8 KB)", "Too big for the editor (max 8 KB)");
        draw_footer();
    }
}

void fm_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    bool redraw = false;

    s_msg = "";
    /* Deleting takes SHIFT twice; any other key cancels */
    if (s_del_armed) {
        s_del_armed = false;
        if (k == KEY_SHIFT) { fm_delete(); return; }
        draw_footer();
    }

    if (k == KEY_DOWN) {
        if (s_cursor < s_nitem - 1) {
            s_cursor++;
            if (s_cursor >= s_scroll + FM_VISIBLE_ROWS) s_scroll++;
            redraw = true;
        }
    } else if (k == KEY_UP) {
        if (s_cursor > 0) {
            s_cursor--;
            if (s_cursor < s_scroll) s_scroll--;
            redraw = true;
        }
    } else if (key_is_exe(k)) {
        fm_open();
        return;
    } else if (k == KEY_SHIFT) {
        if (s_nitem > 0) { s_del_armed = true; draw_footer(); }
        return;
    } else if (k == KEY_VAR) {
        text_editor_new();
        kernel_set_app(APP_TEXT_EDITOR);
        return;
    } else if (k == KEY_BACK || k == KEY_HOME) {
        kernel_set_app(APP_HOME);
        return;
    }

    if (redraw) {
        draw_header();
        draw_list();
        draw_footer();
    }
}

void fm_init(void) {
    s_cursor = 0;
    s_scroll = 0;
}
