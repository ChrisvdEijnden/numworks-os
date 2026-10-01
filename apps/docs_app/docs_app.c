
/* ================================================================
 * NumWorks OS — Docs App (built-in reference)
 * File: apps/docs_app/docs_app.c
 * ================================================================ */
#include "docs_app.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../include/config.h"
#include <string.h>
#include <stdio.h>

#define C_BG  RGB(10,10,20)
#define C_HDR RGB(30,80,200)
#define HEADER_H 24
#define LINE_H   12

typedef struct { const char *title; const char *lines[10]; int nlines; } doc_t;
static const doc_t DOCS[] = {
    { "Shell",
      { "ls        - bestanden tonen",
        "cat <f>   - bestand lezen",
        "touch <f> - leeg bestand maken",
        "rm <f>    - bestand verwijderen",
        "echo <t>  - tekst tonen",
        "run <f.py>- Python uitvoeren",
        "mem       - geheugen tonen",
        "fm        - bestandsbeheer",
        "reboot    - herstarten",
        "ALPHA: letters  SHIFT: = _ [ ]" }, 10 },
    { "Python",
      { "import math, display",
        "display.fill(kleur)",
        "display.str(x,y,t[,fg,bg])",
        "display.pixel(x,y,kleur)",
        "display.fill_rect(x,y,b,h,k)",
        "display.rgb(r,g,b) -> kleur",
        "display.flush() - tonen",
        "open(n).read(), open(n,'w')",
        "os.listdir() remove() stat()",
        "BACK stopt een script" }, 10 },
    { "Rekenen",
      { "+ - * / ^  ( )  x  pi  e  Ans",
        "sin cos tan sqrt ln log exp",
        "SHIFT: asin acos atan cbrt",
        "2x en 3(x+1): impliciet *",
        "Kwadratisch: ax^2+bx+c=0",
        "Lineair: ax+by=c, dx+ey=f",
        "Enkel: f(x)=0 (Newton)",
        "ALPHA: veld bewerken",
        "OK: oplossen / bevestigen",
        "" }, 9 },
    { "Functies",
      { "Voer f(x) in, bijv. x*sin(x)",
        "UP/DOWN: functie kiezen",
        "OK: opslaan, leeg+OK: wissen",
        "TOOLBOX: grafiek",
        "VAR: tabel",
        "ALPHA: terug naar invoer",
        "Grafiek: pijlen = pannen",
        "  + / -: in/uitzoomen",
        "Tabel: UP/DOWN functie",
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
        char tab[20]; snprintf(tab,sizeof(tab),"%d.%s",i+1,DOCS[i].title);  /* short titles fit the 76-px tabs */
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
