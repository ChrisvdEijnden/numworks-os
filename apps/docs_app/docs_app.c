
/* ================================================================
 * NumWorks OS — Docs App (built-in reference)
 * File: apps/docs_app/docs_app.c
 * ================================================================ */
#include "docs_app.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include "../../include/string.h"
#include "../../include/stdio.h"

#define C_BG  RGB(10,10,20)
#define C_HDR RGB(30,80,200)
#define HEADER_H 24
#define LINE_H   12

typedef struct { const char *title; const char *lines[10]; int nlines; } doc_t;
static const doc_t DOCS[] = {
    { "Shell Opdrachten",
      { "ls        - bestanden tonen",
        "cat <f>   - bestand lezen",
        "touch <f> - bestand aanmaken",
        "rm <f>    - bestand verwijderen",
        "mkdir <d> - map aanmaken",
        "echo <t>  - tekst tonen",
        "run <f.py>- Python uitvoeren",
        "mem       - geheugen tonen",
        "fm        - bestandsbeheer",
        "reboot    - herstarten" }, 10 },
    { "Python Modules",
      { "import math     - wiskundige functies",
        "import utime    - tijdfuncties",
        "import display  - scherm tekenen",
        "import ustruct  - binaire data",
        "import uos      - bestandssysteem",
        "display.fill(c) - achtergrond vullen",
        "display.str(x,y,t,fg,bg)",
        "display.flush() - scherm bijwerken",
        "utime.sleep_ms(n)",
        "math.sin/cos/sqrt/log" }, 10 },
    { "Vergelijkingen",
      { "Kwadratisch: ax^2+bx+c=0",
        "  Discriminant D=b^2-4ac",
        "  D>0: twee reele wortels",
        "  D=0: een dubbele wortel",
        "  D<0: complexe wortels",
        "Lineair stelsel: Cramer",
        "  det = ae - bd",
        "  x = (ce-bf)/det",
        "  y = (af-cd)/det",
        "" }, 9 },
    { "Functies Grafiek",
      { "Voer f(x) in bijv: sin(x)*x",
        "TOOLBOX: grafiek tonen",
        "VAR:     tabel tonen",
        "ALPHA:   invoer terugkeren",
        "Grafiek navigatie:",
        "  LEFT/RIGHT: pannen",
        "  UP/DOWN: omhoog/omlaag",
        "  +/-: inzoomen/uitzoomen",
        "Tabel: UP/DOWN functie kiezen",
        "" }, 9 },
};
#define NDOCS  4

static int s_doc  = 0;
static int s_scroll = 0;

void docs_redraw(void) {
    display_fill(C_BG);
    display_fill_rect(0,0,LCD_WIDTH,HEADER_H,C_HDR);
    display_str(6, 6, "Documentatie", WHITE, C_HDR);
    /* Tab bar */
    for (int i=0; i<NDOCS; i++) {
        uint16_t tc = (i==s_doc)?WHITE:RGB(150,150,200);
        uint16_t bg = (i==s_doc)?RGB(50,80,160):C_BG;
        char tab[20]; snprintf(tab,sizeof(tab),"%d.%s",i+1,DOCS[i].title);
        int tx = 4 + i*78;
        display_fill_rect(tx, HEADER_H, 76, 14, bg);
        display_str(tx+2, HEADER_H+3, tab, tc, bg);
    }
    /* Content */
    const doc_t *d = &DOCS[s_doc];
    for (int i=s_scroll; i<d->nlines && i<s_scroll+12; i++) {
        int y = HEADER_H+18+(i-s_scroll)*LINE_H;
        display_str(8, y, d->lines[i], RGB(200,230,255), C_BG);
    }
    display_str(4, LCD_HEIGHT-12,
                "L/R:Kiezen  UP/DN:Scrollen  HOME:Terug",
                YELLOW, C_BG);
}

void docs_init(void) { s_doc=0; s_scroll=0; }

void docs_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;
    if (k==KEY_HOME||k==KEY_BACK) { kernel_set_app(APP_HOME); return; }
    if (k==KEY_LEFT  && s_doc>0)        { s_doc--; s_scroll=0; docs_redraw(); }
    else if (k==KEY_RIGHT && s_doc<NDOCS-1) { s_doc++; s_scroll=0; docs_redraw(); }
    else if (k==KEY_DOWN && s_scroll+1 < DOCS[s_doc].nlines) { s_scroll++; docs_redraw(); }
    else if (k==KEY_UP && s_scroll>0) { s_scroll--; docs_redraw(); }
}
