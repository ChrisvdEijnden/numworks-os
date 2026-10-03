#include "../../apps/text_editor/text_editor.c"
const char *et_name(void) { return s_filename; }
bool et_modified(void) { return s_modified; }
/* The layout, for tests that read the screen */
const int et_top = TOP, et_char_h = CHAR_H, et_char_w = CHAR_W, et_text_x = TEXT_X, et_cols = COLS,
          et_rows = ROWS, et_footer_y = FOOTER_Y, et_gutter = GUTTER;
const uint16_t et_keyword = C_KEYWORD, et_comment = C_COMMENT, et_number = C_NUMBER, et_string = C_STRING;
