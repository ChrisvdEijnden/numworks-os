/* ================================================================
 * NumWorks OS — Graphical File Manager
 * File: ui/filemanager.c
 *
 * A lightweight file browser for the 320×240 display.
 *
 * Layout:
 *   ┌──────────────────────────────────────────┐
 *   │ FILES              [1/8]      [BACK:HOME] │  ← header bar
 *   ├──────────────────────────────────────────┤
 *   │ > hello.py         1.2 KB                │  ← selected
 *   │   main.py          0.4 KB                │
 *   │   notes.txt        2.0 KB                │
 *   │   ...                                    │
 *   ├──────────────────────────────────────────┤
 *   │ [EXE]Open [SHIFT]Del [ALPHA]Rename [+]Copy│  ← hint bar
 *   └──────────────────────────────────────────┘
 *
 * Code size target: < 5 KB
 * ================================================================ */
#include "filemanager.h"
#include "../hal/display.h"
#include "../hal/keyboard.h"
#include "../fs/flashfs.h"
#include "../kernel/kernel.h"
#include "../shell/shell.h"
#include <string.h>
#include <stdio.h>

#define FM_VISIBLE_ROWS 12
#define ITEM_H          16
#define HEADER_H        20
#define FOOTER_H        18
#define LIST_Y          HEADER_H
#define FOOTER_Y        (LCD_HEIGHT - FOOTER_H)

typedef struct {
    char     name[FFS_NAME_LEN];
    uint32_t size;
} fm_item_t;

static fm_item_t s_items[FFS_MAX_FILES];
static int       s_nitem  = 0;
static int       s_cursor = 0;
static int       s_scroll = 0;

/* ── Load file list ──────────────────────────────────────────── */
static void list_cb(const ffs_entry_t *e, void *ctx) {
    int *n = (int *)ctx;
    if (*n < FFS_MAX_FILES) {
        strncpy(s_items[*n].name, e->name, FFS_NAME_LEN-1);
        s_items[*n].size = e->size;
        (*n)++;
    }
}

static void fm_load(void) {
    s_nitem = 0;
    flashfs_ls(list_cb, &s_nitem);
}

/* ── Drawing ─────────────────────────────────────────────────── */
static void draw_row(int row, int item_idx, bool selected) {
    int y = LIST_Y + row * ITEM_H;
    uint16_t bg = selected ? BLUE  : BLACK;
    uint16_t fg = selected ? WHITE : GREEN;

    display_fill_rect(0, y, LCD_WIDTH, ITEM_H, bg);

    if (item_idx < s_nitem) {
        char line[48];
        uint32_t kb10 = (s_items[item_idx].size * 10) / 1024;
        snprintf(line, sizeof(line), " %-22s %3lu.%lu KB",
                 s_items[item_idx].name,
                 (unsigned long)(kb10/10),
                 (unsigned long)(kb10%10));
        display_str(0, y + 3, line, fg, bg);
    }
}

static void draw_header(void) {
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, DKGREY);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), " FILES  %d/%d  [HOME=back]", s_cursor+1, s_nitem);
    display_str(0, 3, hdr, CYAN, DKGREY);
}

static void draw_footer(void) {
    display_fill_rect(0, FOOTER_Y, LCD_WIDTH, FOOTER_H, DKGREY);
    display_str(2, FOOTER_Y+3,
        "EXE:Open  SHIFT:Del  ALPHA:Ren  BACK:Quit",
        YELLOW, DKGREY);
}

void fm_redraw(void) {
    fm_load();
    display_fill(BLACK);
    draw_header();
    draw_footer();
    for (int r = 0; r < FM_VISIBLE_ROWS; r++) {
        draw_row(r, s_scroll + r, (s_scroll + r) == s_cursor);
    }
}

/* ── Input ───────────────────────────────────────────────────── */
static void fm_delete(void) {
    if (s_nitem == 0) return;
    flashfs_delete(s_items[s_cursor].name);
    fm_redraw();
}

static void fm_open(void) {
    if (s_nitem == 0) return;
    /* If .py, run it in shell */
    const char *name = s_items[s_cursor].name;
    int nlen = (int)strlen(name);
    kernel_set_app(APP_SHELL);
    if (nlen > 3 && strcmp(name + nlen - 3, ".py") == 0) {
        char cmd[FFS_NAME_LEN + 6];
        snprintf(cmd, sizeof(cmd), "run %s", name);
        shell_print("> %s\n", cmd);
        /* Delegate to command runner */
        extern void cmd_run(const char *);
        cmd_run(cmd);
    } else {
        char cmd[FFS_NAME_LEN + 6];
        snprintf(cmd, sizeof(cmd), "cat %s", name);
        extern void cmd_run(const char *);
        cmd_run(cmd);
    }
}

void fm_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    bool redraw = false;

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
    } else if (k == KEY_EXE) {
        fm_open();
        return;
    } else if (k == KEY_SHIFT) {
        fm_delete();
        return;
    } else if (k == KEY_BACK || k == KEY_HOME) {
        kernel_set_app(APP_SHELL);
        return;
    }

    if (redraw) {
        draw_header();
        for (int r = 0; r < FM_VISIBLE_ROWS; r++) {
            draw_row(r, s_scroll + r, (s_scroll + r) == s_cursor);
        }
    }
}

void fm_init(void) {
    s_cursor = 0;
    s_scroll = 0;
}
