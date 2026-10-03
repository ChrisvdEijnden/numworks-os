
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
 *  - Line numbers in a grey margin; Python files in colour (keywords,
 *    numbers, strings, comments), the colours of NumWorks' own editor
 * ================================================================ */
#include "text_editor.h"
#include "../../ui/lang.h"
#include "../../ui/theme.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../fs/flashfs.h"
#include "../../include/config.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#define TOP      UI_TITLE_H
#define FOOTER_H 20
#define FOOTER_Y (LCD_HEIGHT - FOOTER_H)
#define CHAR_W   7                                        /* font_small */
#define CHAR_H   14
#define GUTTER   32                                       /* line numbers */
#define TEXT_X   (GUTTER + 4)
#define COLS     ((LCD_WIDTH - TEXT_X - 2) / CHAR_W)      /* 40 */
#define ROWS     ((FOOTER_Y - TOP) / CHAR_H)              /* 14 */
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

/* ── Python in colour ───────────────────────────────────────── */
#define C_KEYWORD  RGB(0xFF, 0x00, 0x0C)
#define C_OPERATOR RGB(0xD7, 0x3A, 0x49)
#define C_STRING   RGB(0x03, 0x2F, 0x62)
#define C_COMMENT  RGB(0x99, 0x99, 0x88)
#define C_NUMBER   RGB(0x1C, 0x00, 0xCF)
#define C_BUILTIN  RGB(0x00, 0x86, 0xB3)

static const char *const KEYWORDS[] = {
    "False", "None", "True", "and", "as", "assert", "break", "class", "continue", "def", "del", "elif",
    "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda",
    "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
};
static const char *const BUILTINS[] = {
    "abs", "bool", "chr", "dict", "divmod", "enumerate", "float", "input", "int", "len", "list", "max",
    "min", "open", "ord", "pow", "print", "range", "round", "set", "sorted", "str", "sum", "tuple", "type",
};

static bool listed(const char *const *list, int n, const char *s, int len) {
    for (int i = 0; i < n; i++)
        if ((int)strlen(list[i]) == len && !memcmp(list[i], s, (size_t)len)) return true;
    return false;
}

static bool is_py(void) {
    size_t n = strlen(s_filename);
    return n > 3 && !strcmp(s_filename + n - 3, ".py");
}

/* Runs of one colour, drawn as far as they fall in the visible columns */
static struct { const char *line; int16_t y; int first, a, b; uint16_t colour; } s_run;

static void run_flush(void) {
    int a = s_run.a > s_run.first ? s_run.a : s_run.first;
    int b = s_run.b < s_run.first + COLS ? s_run.b : s_run.first + COLS;
    if (b > a)
        display_text_n((int16_t)(TEXT_X + (a - s_run.first) * CHAR_W), s_run.y, s_run.line + a, b - a,
                       &font_small, s_run.colour, WHITE);
    s_run.a = s_run.b;
}

static void run_add(int a, int b, uint16_t colour) {
    if (colour != s_run.colour || a != s_run.b) { run_flush(); s_run.a = a; s_run.colour = colour; }
    s_run.b = b;
}

/* One line, len characters, from column first on */
static void draw_code(int16_t y, const char *s, int len, int first) {
    s_run.line = s; s_run.y = y; s_run.first = first;
    s_run.a = s_run.b = 0; s_run.colour = T_TEXT;
    if (!is_py()) { run_add(0, len, T_TEXT); run_flush(); return; }
    int i = 0;
    while (i < len) {
        char c = s[i];
        int j = i + 1;
        uint16_t colour = T_TEXT;
        if (c == '#') {
            j = len;
            colour = C_COMMENT;
        } else if (c == '"' || c == '\'') {
            while (j < len && s[j] != c) j += s[j] == '\\' ? 2 : 1;
            if (j < len) j++;
            if (j > len) j = len;
            colour = C_STRING;
        } else if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < len && s[i + 1] >= '0' && s[i + 1] <= '9')) {
            while (j < len && (isalnum((unsigned char)s[j]) || s[j] == '.' || s[j] == '_')) j++;
            colour = C_NUMBER;
        } else if (isalpha((unsigned char)c) || c == '_') {
            while (j < len && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            if (listed(KEYWORDS, (int)(sizeof KEYWORDS / sizeof *KEYWORDS), s + i, j - i)) colour = C_KEYWORD;
            else if (listed(BUILTINS, (int)(sizeof BUILTINS / sizeof *BUILTINS), s + i, j - i)) colour = C_BUILTIN;
        } else if (strchr("+-*/%=<>!&|^~@", c)) {
            colour = C_OPERATOR;
        }
        run_add(i, j, colour);
        i = j;
    }
    run_flush();
}

/* ── Drawing ───────────────────────────────────────────────────── */
static void draw_line(int linenum, bool active) {
    int vis = linenum - s_scroll;
    if (vis < 0 || vis >= ROWS) return;
    int16_t y = (int16_t)(TOP + vis * CHAR_H);
    display_fill_rect(0, y, GUTTER, CHAR_H, T_GRAY_WHITE);
    display_fill_rect(GUTTER, y, LCD_WIDTH - GUTTER, CHAR_H, WHITE);
    char num[16];
    snprintf(num, sizeof num, "%d", linenum + 1);
    ui_text_right(GUTTER - 3, y, num, &font_small, active ? T_GRAY_DARKEST : T_GRAY_DARK, T_GRAY_WHITE);

    /* The visible part: columns s_hscroll .. s_hscroll+COLS-1 */
    int ls = line_start(linenum);
    int le = ls;
    while (le < s_tlen && s_text[le] != '\n') le++;
    draw_code(y, s_text + ls, le - ls, s_hscroll);

    /* More text to the left or right: a thin marker at that edge */
    if (s_hscroll > 0 && le - ls > 0)
        display_fill_rect(GUTTER + 1, y + 2, 1, CHAR_H - 4, T_GRAY_DARK);
    if (le - ls > s_hscroll + COLS)
        display_fill_rect(LCD_WIDTH - 1, y + 2, 1, CHAR_H - 4, T_GRAY_DARK);

    if (active) {
        int cx = TEXT_X + (s_cpos - ls - s_hscroll) * CHAR_W;
        if (cx >= TEXT_X && cx < LCD_WIDTH) display_fill_rect((int16_t)(cx - 1), y, 1, CHAR_H, T_TEXT);
    }
}

static void draw_all(void) {
    int curline = cursor_line();
    int nlines  = count_lines();
    for (int r=0; r<ROWS; r++) {
        int ln = s_scroll + r;
        if (ln < nlines) draw_line(ln, ln==curline);
        else {
            display_fill_rect(0, (int16_t)(TOP + r * CHAR_H), GUTTER, CHAR_H, T_GRAY_WHITE);
            display_fill_rect(GUTTER, (int16_t)(TOP + r * CHAR_H), LCD_WIDTH - GUTTER, CHAR_H, WHITE);
        }
    }
    int16_t below = (int16_t)(TOP + ROWS * CHAR_H);           /* the few pixels above the footer */
    display_fill_rect(0, below, GUTTER, FOOTER_Y - below, T_GRAY_WHITE);
    display_fill_rect(GUTTER, below, LCD_WIDTH - GUTTER, FOOTER_Y - below, WHITE);
}

/* Title (file name, unsaved marker) and footer (status, key hints,
 * where the cursor is) */
static void draw_bars(void) {
    char title[40];
    snprintf(title, sizeof(title), "%s%s", s_filename[0] ? s_filename : TR("[nieuw]", "[new]"),
             s_modified ? " *" : "");
    ui_title_bar_mods(title, s_shift, s_alpha);

    display_fill_rect(0, FOOTER_Y, LCD_WIDTH, FOOTER_H, T_GRAY_BRIGHT);
    display_hline(0, FOOTER_Y, LCD_WIDTH, T_GRAY_MIDDLE);
    int16_t ty = FOOTER_Y + 4;
    char foot[64];
    if (s_naming && !s_status[0]) {
        snprintf(foot, sizeof(foot), TR("Naam: %s_ (%s)", "Name: %s_ (%s)"),
                 s_name, s_alpha ? (s_shift ? "ABC" : "abc") : "123");
        display_text(6, ty, foot, &font_small, T_TEXT, T_GRAY_BRIGHT);
        return;
    }
    if (s_status[0]) display_text(6, ty, s_status, &font_small, T_TEXT, T_GRAY_BRIGHT);
    else display_text(6, ty, TR("SHIFT+OK:opslaan  HOME:terug", "SHIFT+OK:save  HOME:back"), &font_small,
                      T_GRAY_VDARK, T_GRAY_BRIGHT);
    int cl = cursor_line();
    char pos[24];
    snprintf(pos, sizeof(pos), TR("r%d k%d", "L%d C%d"), cl + 1, s_cpos - line_start(cl) + 1);
    ui_text_right(LCD_WIDTH - 6, ty, pos, &font_small, T_GRAY_VDARK, T_GRAY_BRIGHT);
}

void text_editor_redraw(void) {
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
