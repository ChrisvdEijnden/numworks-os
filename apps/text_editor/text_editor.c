
/* ================================================================
 * NumWorks OS — Text Editor
 * File: apps/text_editor/text_editor.c
 *
 * Features:
 *  - Open .txt or .py files from internal FS
 *  - Edit with full keypad input (ALPHA mode for letters)
 *  - Save back to internal FS (SHIFT+EXE)
 *  - New file creation
 *  - Scrolling viewport for files > 24 lines
 * ================================================================ */
#include "text_editor.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../fs/flashfs.h"
#include "../../include/config.h"
#include "../../include/string.h"
#include "../../include/stdio.h"

#define C_BG   RGB(10,12,18)
#define C_HDR  RGB(30,80,200)
#define C_CURS RGB(60,130,255)
#define C_LINE RGB(20,20,32)
#define HEADER_H 24
#define FOOTER_H 14
#define COLS     EDITOR_COLS
#define ROWS     EDITOR_ROWS
#define CHAR_W   7
#define CHAR_H   10
#define MAX_B    EDITOR_MAX_BYTES

static char  s_text[MAX_B+1];
static int   s_tlen = 0;
static int   s_cpos = 0;  /* cursor byte position */
static int   s_scroll = 0;  /* top visible line */
static char  s_filename[FFS_NAME_LEN] = "";
static bool  s_modified = false;
static bool  s_shift = false;
static bool  s_alpha = false;
static const char *s_status = "";   /* one-shot message in the footer */

/* Count lines */
static int count_lines(void) {
    int n = 1;
    for (int i=0; i<s_tlen; i++) if (s_text[i]=='\n') n++;
    return n;
}

/* Get line start positions (up to ROWS+scroll) */
static int line_start(int linenum) {
    int l=0;
    for (int i=0; i<s_tlen; i++) {
        if (l == linenum) return i;
        if (s_text[i]=='\n') l++;
    }
    return s_tlen;
}

static int cursor_line(void) {
    int l=0;
    for (int i=0; i<s_cpos; i++) if(s_text[i]=='\n') l++;
    return l;
}

static void draw_line(int linenum, bool active) {
    int vis = linenum - s_scroll;
    if (vis < 0 || vis >= ROWS) return;
    int y = HEADER_H + vis*CHAR_H;
    uint16_t bg = active ? C_LINE : C_BG;
    display_fill_rect(0, y, LCD_WIDTH, CHAR_H, bg);

    int ls = line_start(linenum);
    char linebuf[COLS+1];
    int col = 0;
    for (int i=ls; i<s_tlen && s_text[i]!='\n' && col<COLS; i++,col++)
        linebuf[col] = s_text[i];
    linebuf[col] = 0;

    /* Cursor within this line */
    int cur_col = -1;
    if (active) {
        int ls2 = line_start(linenum);
        cur_col = s_cpos - ls2;
        if (cur_col < 0 || cur_col > col) cur_col = col;
    }

    display_str(0, y, linebuf, RGB(220,230,255), bg);

    if (cur_col >= 0) {
        int cx = cur_col * CHAR_W;
        display_fill_rect(cx, y, 2, CHAR_H, C_CURS);
    }
}

static void draw_all(void) {
    int curline = cursor_line();
    int nlines  = count_lines();
    for (int r=0; r<ROWS; r++) {
        int ln = s_scroll + r;
        if (ln < nlines) draw_line(ln, ln==curline);
        else display_fill_rect(0, HEADER_H+r*CHAR_H, LCD_WIDTH, CHAR_H, C_BG);
    }
}

/* Header (file name, unsaved marker) and footer (status or key hints) */
static void draw_bars(void) {
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, C_HDR);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "Editor: %s%s",
             s_filename[0]?s_filename:"[nieuw]",
             s_modified?" *":"");
    display_str(6, 6, hdr, WHITE, C_HDR);

    display_fill_rect(0, LCD_HEIGHT-FOOTER_H, LCD_WIDTH, FOOTER_H, RGB(25,25,40));
    char foot[64];
    if (s_status[0])
        snprintf(foot, sizeof(foot), "%s", s_status);
    else
        snprintf(foot, sizeof(foot), "%s%s  SHIFT+OK:Opslaan  HOME:Terug",
                 s_shift?"SHF ":"", s_alpha?"ABC":"   ");
    display_str(2, LCD_HEIGHT-FOOTER_H+3, foot,
                s_status[0] ? CYAN : YELLOW, RGB(25,25,40));
}

void text_editor_redraw(void) {
    display_fill(C_BG);
    draw_all();
    draw_bars();
}

void text_editor_init(void) {
    s_tlen=0; s_cpos=0; s_scroll=0; s_modified=false;
    s_shift=false; s_alpha=false; s_filename[0]=0; s_text[0]=0;
}

bool text_editor_open(const char *name) {
    uint32_t off, sz;
    /* Refuse rather than open a truncated or empty copy: saving that
     * under the same name would destroy the file. */
    if (flashfs_open_read(name, &off, &sz) != 0 || sz > MAX_B) return false;
    if (flashfs_read(off, s_text, sz) != (int)sz) return false;
    s_tlen = (int)sz; s_text[sz] = 0;
    strncpy(s_filename, name, FFS_NAME_LEN-1);
    s_filename[FFS_NAME_LEN-1] = 0;
    s_cpos=0; s_scroll=0; s_modified=false;
    s_status = "";
    return true;
}

static void save_file(void) {
    if (!s_filename[0]) {
        /* Prompt — for now use default name */
        strncpy(s_filename, "noname.txt", FFS_NAME_LEN-1);
    }
    if (flashfs_write(s_filename, s_text, (uint32_t)s_tlen) == s_tlen) {
        s_modified = false;
        s_status = "Opgeslagen";
    } else {
        s_status = "Opslaan mislukt!";   /* keep the changes marked unsaved */
    }
}

static void insert_char(char c) {
    if (s_tlen >= MAX_B) return;
    memmove(s_text+s_cpos+1, s_text+s_cpos, s_tlen-s_cpos+1);
    s_text[s_cpos++] = c;
    s_tlen++;
    s_modified = true;
}

static void delete_char(void) {
    if (s_cpos == 0) return;
    memmove(s_text+s_cpos-1, s_text+s_cpos, s_tlen-s_cpos+1);
    s_cpos--; s_tlen--;
    s_modified = true;
}

void text_editor_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    int cl = cursor_line();

    if (k==KEY_HOME||k==KEY_BACK) { kernel_set_app(APP_HOME); return; }
    s_status = "";
    if (k==KEY_SHIFT) { s_shift=!s_shift; text_editor_redraw(); return; }
    if (k==KEY_ALPHA) { s_alpha=!s_alpha; text_editor_redraw(); return; }

    if (key_is_exe(k) && s_shift) { save_file(); s_shift = false; text_editor_redraw(); return; }

    if (k==KEY_BACKSPACE) { delete_char(); }
    else if (key_is_exe(k)) { insert_char('\n'); }
    else if (k==KEY_LEFT  && s_cpos>0) s_cpos--;
    else if (k==KEY_RIGHT && s_cpos<s_tlen) s_cpos++;
    else if (k==KEY_UP) {
        if (cl > 0) {
            int ls = line_start(cl-1);
            int col_cur = s_cpos - line_start(cl);
            int ls_prev_end = line_start(cl) - 1;
            int prev_len = ls_prev_end - ls;
            if (prev_len < 0) prev_len=0;
            s_cpos = ls + (col_cur < prev_len ? col_cur : prev_len);
        }
    }
    else if (k==KEY_DOWN) {
        int nlines = count_lines();
        if (cl < nlines-1) {
            int col_cur = s_cpos - line_start(cl);
            int ls_next = line_start(cl+1);
            int next_end = line_start(cl+2);
            int next_len = (cl+2 < nlines) ? next_end - ls_next - 1 : s_tlen - ls_next;
            if (next_len < 0) next_len=0;
            s_cpos = ls_next + (col_cur < next_len ? col_cur : next_len);
        }
    }
    else {
        char c = key_to_char(k, s_shift, s_alpha);
        if (c) { insert_char(c); s_shift=false; }
    }

    /* Scroll to keep cursor visible */
    cl = cursor_line();
    if (cl < s_scroll) s_scroll = cl;
    if (cl >= s_scroll + ROWS) s_scroll = cl - ROWS + 1;

    draw_all();
    draw_bars();
}
