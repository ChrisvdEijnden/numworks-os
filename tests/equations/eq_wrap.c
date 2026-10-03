/* Built with the project's headers only; exposes the app's statics */
#include "../../apps/equations/equations.c"
char *eqt_single(void) { return s_single; }
char *eqt_input(void)  { return s_input; }
const int eqt_result_y = RESULT_Y, eqt_hint_y = HINT_Y;   /* where the answers go */
