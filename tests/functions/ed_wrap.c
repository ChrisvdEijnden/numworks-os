#include "../../apps/text_editor/text_editor.c"
const char *et_name(void) { return s_filename; }
bool et_modified(void) { return s_modified; }
