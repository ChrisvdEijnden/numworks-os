#include "../../apps/python_app/python_app.c"
/* test access to the app's state */
void pa_type(const char *t) { while (*t && s_len < LINE_MAX - 1) s_line[s_len++] = *t++; s_line[s_len] = 0; }
const char *pa_line(void) { return s_line; }
bool pa_cont(void) { return s_cont; }
/* the whole output ring as one string, oldest first */
void pa_output(char *buf, int max) {
    buf[0] = 0;
    int first = s_nout - (OROWS - 1); if (first < 0) first = 0;
    for (int li = first; li <= s_nout; li++) { strncat(buf, s_out[li % OROWS], max - strlen(buf) - 2); strcat(buf, "\n"); }
}
void pa_clear(void) { s_nout = 0; memset(s_out, 0, sizeof(s_out)); }
