/* NumWorks OS — interface language (see lang.h) */
#include "lang.h"

lang_t g_lang = LANG_NL;

void lang_set(lang_t l) {
    if (l < LANG_COUNT) g_lang = l;
}

const char *lang_name(lang_t l) {
    return l == LANG_EN ? "English" : "Nederlands";
}
