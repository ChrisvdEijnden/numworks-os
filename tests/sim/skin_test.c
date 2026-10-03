/* The simulator's reading of NumWorks' layout.json (sim/sim_skin.c):
 * a copy in the shape of theirs, made up for the test, not theirs */
#include <stdio.h>
#include <string.h>
#include "../../sim/sim_skin.h"

static int fails;
static void check(int c, const char *w) { printf("  %s %s\n", c ? "ok  " : "FAIL", w); if (!c) fails++; }

static const skin_key_t *find(const skin_layout_t *l, key_code_t k) {
    for (int i = 0; i < l->nkeys; i++) if (l->keys[i].key == k) return &l->keys[i];
    return NULL;
}

int main(void) {
    const char *json =
        "{\n  \"background\": [0, 0, 1000, 2000],\n  \"area_of_interest\": [0, 0, 1000, 2000],\n"
        "  \"screen\": [100, 150, 800, 600],\n"
        "  \"shapes\": { \"Round\": [\"OK\", \"Back\"] },\n"
        "  \"keys\": {\n    \"Left\": [10, 900, 120, 80],\n\n    \"OK\": [700, 950, 125, 125],\n"
        "    \"Imaginary\": [580, 1338, 126, 85],\n    \"Mystery\": [1, 2, 3, 4],\n"
        "    \"EXE\": [862, 2003, 153, 98]\n  },\n  \"loader_color\": \"#fff\"\n}\n";
    skin_layout_t l;
    check(sim_skin_parse_layout(json, &l), "a layout is read");
    check(l.bg_w == 1000 && l.bg_h == 2000, "its size");
    check(l.screen[0] == 100 && l.screen[1] == 150 && l.screen[2] == 800 && l.screen[3] == 600, "the screen");
    const skin_key_t *ok = find(&l, KEY_OK), *exe = find(&l, KEY_EXE), *im = find(&l, KEY_IMAG);
    check(l.nkeys == 4 && ok && ok->x == 700 && ok->w == 125 && exe && exe->y == 2003 && exe->h == 98 && im &&
          find(&l, KEY_LEFT), "the keys, by NumWorks' names; an unknown one skipped");
    check(!sim_skin_parse_layout("{\"screen\": [1, 2, 3, 4]}", &l), "no keys: not a layout");
    check(!sim_skin_parse_layout("{\"background\": [0, 0, 1, 1], \"screen\": [1, 2,", &l), "cut off: not a layout");
    check(!sim_skin_parse_layout("", &l), "empty: not a layout");
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
