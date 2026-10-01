#pragma once
/* User interface language. Every visible text goes through TR(),
 * which picks the Dutch or the English version. */
typedef enum { LANG_NL, LANG_EN, LANG_COUNT } lang_t;

extern lang_t g_lang;
#define TR(nl, en) (g_lang == LANG_EN ? (en) : (nl))

void        lang_set(lang_t l);
const char *lang_name(lang_t l);    /* in that language: "Nederlands", "English" */
