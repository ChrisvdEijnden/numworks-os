#include "../../apps/tetris/tetris.c"
int  tt_py(void) { return s_py; }
int  tt_px(void) { return s_px; }
void tt_set_level(int lv) { s_lines = lv * 10; }
void tt_clear_lines(void) { clear_lines(); }
unsigned tt_drop_ms(void) { return s_drop_ms; }
void tt_state(int *type, int *rot) { *type = s_ptype; *rot = s_prot; }
