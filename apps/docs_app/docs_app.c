/* ================================================================
 * NumWorks OS — Docs App (built-in reference), Dutch and English
 * File: apps/docs_app/docs_app.c
 * ================================================================ */
#include "docs_app.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../ui/lang.h"
#include "../../ui/theme.h"
#include <string.h>
#include <stdio.h>

#define HEADER_H (UI_TITLE_H + UI_TAB_H)
#define LINE_H   14
#define TEXT_Y   (HEADER_H + 8)
#define SHOWN    ((LCD_HEIGHT - TEXT_Y - 4) / LINE_H)    /* lines on screen: 13 */
#define MAXL     16

typedef struct { const char *title; const char *lines[MAXL]; } page_t;

static const page_t NL[] = {
    { "Rekenen", {
        "+ - * / ^  ( )  x  pi  e  Ans",
        "sin cos tan sqrt ln log exp",
        "SHIFT: asin acos atan cbrt e^x",
        "2x en 3(x+1): impliciet *",
        "EXE: uitrekenen",
        "UP/DOWN: eerdere sommen",
        "  L/R: som of uitkomst",
        "  OK: in de invoer zetten",
        "  <-: uit de geschiedenis",
        "Vergelijkingen:",
        "  ax^2+bx+c=0, 2x2-stelsel,",
        "  f(x)=0 (Newton)",
        "  ALPHA: veld bewerken", NULL } },
    { "Grafiek", {
        "Voer f(x) in, bijv. x*sin(x)",
        "OK: opslaan, leeg+OK: wissen",
        "TOOLBOX: grafiek, VAR: tabel",
        "Grafiek: pijlen = schuiven,",
        "  + / -: in/uitzoomen",
        "OK: volgen langs de grafiek",
        "  L/R: verplaatsen",
        "  UP/DOWN: andere functie",
        "TOOLBOX: analyse, rechts van",
        "  de cursor: nulpunt, minimum,",
        "  maximum, snijpunt",
        "BACK: volgen stoppen", NULL } },
    { "Statistiek", {
        "Gegevens: lijsten X en Y",
        "  typ een getal, OK: opslaan",
        "  <-: cel wissen",
        "  UP bovenaan: tabbladen",
        "Statistiek: n, som, gem.,",
        "  mediaan, Q1, Q3, min, max,",
        "  bereik, sd (pop / steekpr.)",
        "Met X en Y: regressie",
        "  y = ax + b, r en r^2",
        "Grafiek: spreidingsdiagram,",
        "  of boxplot + histogram",
        "Opgeslagen in stats.csv", NULL } },
    { "Python", {
        "import math, time, random, os",
        "display: fill str pixel",
        "  fill_rect rgb flush",
        "kandinsky: fill_rect",
        "  set_pixel get_pixel",
        "  draw_string color",
        "ion.keydown(ion.KEY_OK)",
        "open(n).read(), open(n,'w')",
        "os.listdir() remove() stat()",
        "BACK: script stoppen",
        "Lege regel + EXE: blok", NULL } },
    { "Shell", {
        "ls        - bestanden tonen",
        "cat <f>   - bestand lezen",
        "touch <f> - leeg bestand maken",
        "rm <f>    - bestand verwijderen",
        "run <f.py>- Python uitvoeren",
        "mem       - geheugen tonen",
        "bat       - batterij tonen",
        "fm        - bestandsbeheer",
        "reboot    - herstarten",
        "ALPHA: letters  SHIFT: = _ [ ]", NULL } },
    { "Spellen", {
        "Tetris: L/R schuiven,",
        "  UP draaien, DOWN sneller",
        "Snake: pijlen sturen,",
        "  appels maken je langer",
        "2048: pijlen schuiven, gelijke",
        "  tegels worden samengevoegd",
        "OK: opnieuw na game over",
        "BACK: terug naar Spellen",
        "Records blijven bewaard", NULL } },
};

static const page_t EN[] = {
    { "Calculate", {
        "+ - * / ^  ( )  x  pi  e  Ans",
        "sin cos tan sqrt ln log exp",
        "SHIFT: asin acos atan cbrt e^x",
        "2x and 3(x+1): implicit *",
        "EXE: calculate",
        "UP/DOWN: earlier calculations",
        "  L/R: calculation or result",
        "  OK: put it in the input",
        "  <-: remove from history",
        "Equations:",
        "  ax^2+bx+c=0, 2x2 system,",
        "  f(x)=0 (Newton)",
        "  ALPHA: edit a field", NULL } },
    { "Graph", {
        "Enter f(x), e.g. x*sin(x)",
        "OK: save, empty+OK: delete",
        "TOOLBOX: graph, VAR: table",
        "Graph: arrows = pan,",
        "  + / -: zoom in/out",
        "OK: trace along the graph",
        "  L/R: move",
        "  UP/DOWN: other function",
        "TOOLBOX: analysis, right of",
        "  the cursor: zero, minimum,",
        "  maximum, intersection",
        "BACK: stop tracing", NULL } },
    { "Statistics", {
        "Data: lists X and Y",
        "  type a number, OK: store",
        "  <-: clear a cell",
        "  UP at the top: tabs",
        "Stats: n, sum, mean, median,",
        "  Q1, Q3, min, max, range,",
        "  sd (population / sample)",
        "With X and Y: regression",
        "  y = ax + b, r and r^2",
        "Plot: scatter plot,",
        "  or box plot + histogram",
        "Saved in stats.csv", NULL } },
    { "Python", {
        "import math, time, random, os",
        "display: fill str pixel",
        "  fill_rect rgb flush",
        "kandinsky: fill_rect",
        "  set_pixel get_pixel",
        "  draw_string color",
        "ion.keydown(ion.KEY_OK)",
        "open(n).read(), open(n,'w')",
        "os.listdir() remove() stat()",
        "BACK: stop a script",
        "Empty line + EXE: run block", NULL } },
    { "Shell", {
        "ls        - list files",
        "cat <f>   - show a file",
        "touch <f> - create empty file",
        "rm <f>    - delete a file",
        "run <f.py>- run Python",
        "mem       - memory use",
        "bat       - battery",
        "fm        - file manager",
        "reboot    - restart",
        "ALPHA: letters  SHIFT: = _ [ ]", NULL } },
    { "Games", {
        "Tetris: L/R move,",
        "  UP rotate, DOWN drop",
        "Snake: arrows steer,",
        "  apples make you longer",
        "2048: arrows slide, equal",
        "  tiles merge",
        "OK: again after game over",
        "BACK: back to Games",
        "Best scores are kept", NULL } },
};

#define NPAGES (int)(sizeof(NL) / sizeof(NL[0]))
_Static_assert(sizeof(NL) == sizeof(EN), "every page in both languages");

static int s_page = 0;
static int s_scroll = 0;

static const page_t *page(void) { return g_lang == LANG_EN ? &EN[s_page] : &NL[s_page]; }
static int nlines(const page_t *p) { int n = 0; while (n < MAXL && p->lines[n]) n++; return n; }

/* The page's name on a purple bar, with arrows to the pages beside it */
static void draw_page_bar(const page_t *p) {
    display_fill_rect(0, UI_TITLE_H, LCD_WIDTH, UI_TAB_H, T_PURPLE);
    int16_t ty = UI_TITLE_H + (UI_TAB_H - 14) / 2;
    ui_text_center(LCD_WIDTH / 2, ty, p->title, &font_small, WHITE, T_PURPLE);
    if (s_page > 0) display_text(10, ty, "<", &font_small, WHITE, T_PURPLE);
    if (s_page < NPAGES - 1) display_text(LCD_WIDTH - 17, ty, ">", &font_small, WHITE, T_PURPLE);
    char num[24];
    snprintf(num, sizeof num, "%d/%d", s_page + 1, NPAGES);
    display_text(26, ty, num, &font_small, T_GRAY_MIDDLE, T_PURPLE);
}

void docs_redraw(void) {
    const page_t *p = page();
    ui_title_bar(TR("Help", "Help"));
    draw_page_bar(p);
    display_fill_rect(0, HEADER_H, LCD_WIDTH, LCD_HEIGHT - HEADER_H, WHITE);
    int n = nlines(p);
    for (int i = s_scroll; i < n && i < s_scroll + SHOWN; i++)
        display_text(10, (int16_t)(TEXT_Y + (i - s_scroll) * LINE_H), p->lines[i], &font_small, T_TEXT, WHITE);
    if (n > SHOWN) ui_scrollbar(LCD_WIDTH - 6, TEXT_Y, SHOWN * LINE_H, s_scroll, SHOWN, n);
}

void docs_init(void) { s_page = 0; s_scroll = 0; }

void docs_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    if (k == KEY_HOME || k == KEY_BACK) { kernel_set_app(APP_HOME); return; }
    if (k == KEY_LEFT && s_page > 0)               { s_page--; s_scroll = 0; }
    else if (k == KEY_RIGHT && s_page < NPAGES - 1) { s_page++; s_scroll = 0; }
    else if (k == KEY_DOWN && s_scroll + SHOWN < nlines(page())) s_scroll++;
    else if (k == KEY_UP && s_scroll > 0) s_scroll--;
    else return;
    docs_redraw();
}
