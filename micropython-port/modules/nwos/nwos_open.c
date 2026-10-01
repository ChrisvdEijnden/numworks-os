/* ================================================================
 * NumWorks OS — builtin open() for MicroPython
 *
 * Read-only: open(name[, "r"]) returns an io.StringIO holding the
 * file's text, so read(), readline(), iteration and `with` all work.
 * ================================================================ */
#include <string.h>
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/builtin.h"
#include "../../../fs/flashfs.h"

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)kwargs;
    const char *name = mp_obj_str_get_str(args[0]);
    const char *mode = n_args > 1 ? mp_obj_str_get_str(args[1]) : "r";
    if (strcmp(mode, "r") != 0 && strcmp(mode, "rt") != 0)
        mp_raise_ValueError(MP_ERROR_TEXT("only mode 'r' is supported"));

    uint32_t off, size;
    if (flashfs_open_read(name, &off, &size) != 0) mp_raise_OSError(MP_ENOENT);
    vstr_t vstr;
    vstr_init_len(&vstr, size);
    if (flashfs_read(off, vstr.buf, size) != (int)size) {
        vstr_clear(&vstr);
        mp_raise_OSError(MP_EIO);
    }
    mp_obj_t text = mp_obj_new_str_from_vstr(&vstr);
    return mp_call_function_1(MP_OBJ_FROM_PTR(&mp_type_stringio), text);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
