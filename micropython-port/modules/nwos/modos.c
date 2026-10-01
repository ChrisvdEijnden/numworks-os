/* ================================================================
 * NumWorks OS — `os` module for MicroPython
 *
 * The flash file system is flat (no directories):
 *   os.listdir()         names of all files
 *   os.remove(name)
 *   os.rename(old, new)  fails if `new` exists
 *   os.stat(name)        CPython-style tuple; the size is [6]
 * ================================================================ */
#include "py/runtime.h"
#include "py/mperrno.h"
#include "../../../fs/flashfs.h"

static void add_name(const ffs_entry_t *e, void *ctx) {
    mp_obj_list_append(MP_OBJ_FROM_PTR(ctx), mp_obj_new_str(e->name, strlen(e->name)));
}

static mp_obj_t os_listdir(size_t n_args, const mp_obj_t *args) {
    (void)n_args; (void)args;                    /* one flat directory */
    mp_obj_t list = mp_obj_new_list(0, NULL);
    flashfs_ls(add_name, MP_OBJ_TO_PTR(list));
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_listdir_obj, 0, 1, os_listdir);

static mp_obj_t os_remove(mp_obj_t name_in) {
    const char *name = mp_obj_str_get_str(name_in);
    if (!flashfs_exists(name)) mp_raise_OSError(MP_ENOENT);
    if (flashfs_delete(name) != 0) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_remove_obj, os_remove);

static mp_obj_t os_rename(mp_obj_t from_in, mp_obj_t to_in) {
    const char *from = mp_obj_str_get_str(from_in), *to = mp_obj_str_get_str(to_in);
    if (!flashfs_exists(from)) mp_raise_OSError(MP_ENOENT);
    if (flashfs_exists(to)) mp_raise_OSError(MP_EEXIST);
    if (flashfs_rename(from, to) != 0) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(os_rename_obj, os_rename);

static mp_obj_t os_stat(mp_obj_t name_in) {
    uint32_t off, size;
    if (flashfs_open_read(mp_obj_str_get_str(name_in), &off, &size) != 0)
        mp_raise_OSError(MP_ENOENT);
    mp_obj_t t[10];
    for (int i = 0; i < 10; i++) t[i] = MP_OBJ_NEW_SMALL_INT(0);
    t[0] = MP_OBJ_NEW_SMALL_INT(0x8000);         /* S_IFREG */
    t[6] = mp_obj_new_int_from_uint(size);
    return mp_obj_new_tuple(10, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_stat_obj, os_stat);

static const mp_rom_map_elem_t os_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_os) },
    { MP_ROM_QSTR(MP_QSTR_listdir),  MP_ROM_PTR(&os_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove),   MP_ROM_PTR(&os_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename),   MP_ROM_PTR(&os_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat),     MP_ROM_PTR(&os_stat_obj) },
};
static MP_DEFINE_CONST_DICT(os_globals, os_globals_table);

const mp_obj_module_t nwos_os_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&os_globals,
};
MP_REGISTER_MODULE(MP_QSTR_os, nwos_os_module);
