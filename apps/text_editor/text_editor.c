
/* ================================================================
 * NumWorks OS — Text Editor
 * File: apps/text_editor/text_editor.c
 *
 * Features:
 *  - Open .txt or .py files from internal FS
 *  - Edit with full keypad input (ALPHA mode for letters)
 *  - Save back to internal FS (SHIFT+EXE)
 *  - New file creation
 *  - Files up to the file system's limit (8 KB)
 *  - Scrolls vertically and horizontally to keep the cursor in view
 * ================================================================ */
#include "text_editor.h"
#include "../../ui/lang.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../fs/flashfs.h"
#include "../../include/config.h"
#include <string.h>
#include <stdio.h>

#define C_BG   RGB(10,12,18)
#define C_HDR  RGB(30,80,200)
#define C_CURS RGB(60,130,255)
#define C_LINE RGB(20,20,32)
#define HEADER_H 24
#define FOOTER_H 14
#define CHAR_W   7
#define CHAR_H   10
#define COLS     ((LCD_WIDTH - 2) / CHAR_W)                    /* 45 */
#define ROWS     ((LCD_HEIGHT - HEADER_H - FOOTER_H) / CHAR_H) /* 20 */
#define MAX_B    ((int)EDITOR_MAX_BYTES)

static char  s_text[MAX_B+1];
static int   s_tlen = 0;
static int   s_cpos = 0;  /* cursor byte position */
static int   s_scroll = 0;  /* top visible line */
static int   s_hscroll = 0; /* first visible column */
static char  s_filename[FFS_NAME_LEN] = "";
static bool  s_modified = false;
static bool  s_shift = false;
static bool  s_alpha = false;
static const char *s_status = "";   /* one-shot message in the footer */
/* Asking for a file name before the first save of a new document */
static bool  s_naming = false;
static char  s_name[FFS_NAME_LEN];
static int   s_name_len = 0;

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

    /* The visible part: columns s_hscroll .. s_hscroll+COLS-1 */
    int ls = line_start(linenum);
    int le = ls;
    while (le < s_tlen && s_text[le] != '\n') le++;
    char linebuf[COLS+1];
    int n = 0;
    for (int i = ls + s_hscroll; i < le && n < COLS; i++)
        linebuf[n++] = s_text[i];
    linebuf[n] = 0;
    display_str_len(0, y, linebuf, n, RGB(220,230,255), bg);

    /* More text to the left or right: a thin marker at that edge */
    if (s_hscroll > 0 && le - ls > 0)
        display_fill_rect(0, y + 2, 1, CHAR_H - 4, RGB(120,120,160));
    if (le - ls > s_hscroll + COLS)
        display_fill_rect(LCD_WIDTH - 1, y + 2, 1, CHAR_H - 4, RGB(120,120,160));

    if (active) {
        int cx = (s_cpos - ls - s_hscroll) * CHAR_W;
        if (cx >= 0 && cx < LCD_WIDTH) display_fill_rect(cx, y, 2, CHAR_H, C_CURS);
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
             s_filename[0]?s_filename:TR("[nieuw]", "[new]"),
             s_modified?" *":"");
    display_str(6, 6, hdr, WHITE, C_HDR);
    int cl = cursor_line();
    char pos[24];
    int pn = snprintf(pos, sizeof(pos), TR("r%d k%d", "L%d C%d"), cl + 1, s_cpos - line_start(cl) + 1);
    display_str((int16_t)(LCD_WIDTH - 6 - pn * CHAR_W), 6, pos, RGB(200,220,255), C_HDR);

    display_fill_rect(0, LCD_HEIGHT-FOOTER_H, LCD_WIDTH, FOOTER_H, RGB(25,25,40));
    char foot[64];
    if (s_naming && !s_status[0])
        snprintf(foot, sizeof(foot), TR("Naam: %s_ (%s)", "Name: %s_ (%s)"),
                 s_name, s_alpha ? (s_shift ? "ABC" : "abc") : "123");
    else if (s_status[0])
        snprintf(foot, sizeof(foot), "%s", s_status);
    else
        snprintf(foot, sizeof(foot), TR("%s%s  SHIFT+OK:Opslaan  HOME:Terug", "%s%s  SHIFT+OK:Save  HOME:Back"),
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
    text_editor_new();
    s_shift=false; s_alpha=false;
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
    s_cpos=0; s_scroll=0; s_hscroll=0; s_modified=false;
    s_status = "";
    return true;
}

static void save_file(void) {
    if (!s_filename[0]) {
        /* New document: ask for a name first (letters by default) */
        s_naming = true;
        s_name_len = 0; s_name[0] = 0;
        s_alpha = true; s_shift = false;
        return;
    }
    if (flashfs_write(s_filename, s_text, (uint32_t)s_tlen) == s_tlen) {
        s_modified = false;
        s_status = TR("Opgeslagen", "Saved");
    } else {
        s_status = TR("Opslaan mislukt!", "Saving failed!");   /* keep the changes marked unsaved */
    }
}

static void insert_char(char c) {
    if (s_tlen >= MAX_B) { s_status = TR("Bestand vol (max 8 KB)", "File full (max 8 KB)"); return; }
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

static bool name_char_ok(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

/* Keys while asking for a file name */
static void naming_key(key_code_t k) {
    s_status = "";
    if (k == KEY_HOME || k == KEY_BACK) {
        s_naming = false; s_alpha = false;
        s_status = TR("Niet opgeslagen", "Not saved");
    } else if (k == KEY_SHIFT) {
        s_shift = !s_shift;
    } else if (k == KEY_ALPHA) {
        s_alpha = !s_alpha;
    } else if (k == KEY_BACKSPACE) {
        if (s_name_len > 0) s_name[--s_name_len] = 0;
    } else if (key_is_exe(k)) {
        if (s_name_len == 0) {
            s_status = TR("Geef eerst een naam", "Enter a name first");
        } else if (flashfs_exists(s_name)) {
            s_status = TR("Naam bestaat al, kies een andere", "Name exists, choose another");   /* never overwrite */
        } else {
            strncpy(s_filename, s_name, FFS_NAME_LEN-1);
            s_filename[FFS_NAME_LEN-1] = 0;
            s_naming = false; s_alpha = false;
            save_file();
            if (s_modified) s_filename[0] = 0;   /* failed: stay unnamed */
        }
    } else {
        char c = key_to_char(k, s_shift, s_alpha);
        if (c && name_char_ok(c) && s_name_len < FFS_NAME_LEN-1) {
            s_name[s_name_len++] = c;
            s_name[s_name_len] = 0;
            s_shift = false;
        }
    }
    draw_bars();
}

void text_editor_new(void) {
    s_tlen=0; s_cpos=0; s_scroll=0; s_hscroll=0; s_modified=false;
    s_filename[0]=0; s_text[0]=0;
    s_naming = false; s_status = "";
}

void text_editor_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    int cl = cursor_line();

    if (s_naming) { naming_key(k); return; }
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

    /* Scroll to keep the cursor visible. Scrolling left keeps a few
     * columns of context to the left of the cursor. */
    cl = cursor_line();
    if (cl < s_scroll) s_scroll = cl;
    if (cl >= s_scroll + ROWS) s_scroll = cl - ROWS + 1;
    int cc = s_cpos - line_start(cl);
    if (cc < s_hscroll) s_hscroll = cc > 8 ? cc - 8 : 0;
    if (cc >= s_hscroll + COLS) s_hscroll = cc - COLS + 1;

    draw_all();
    draw_bars();
}
