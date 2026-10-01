/* ================================================================
 * NumWorks OS — builtin open() for MicroPython
 *
 *   open(name)  / "r", "rt"   text, read: an io.StringIO of the file
 *   open(name, "rb")          bytes, read: an io.BytesIO
 *   open(name, "w"/"a")       write text (also "wb"/"ab" for bytes)
 *
 * The flash file system stores whole files, so a file opened for
 * writing collects its contents in RAM and saves them on flush() and
 * close() (or at the end of `with`). Files a script leaves open are
 * saved when the script ends (nwos_close_files()); at the REPL, what an
 * entry wrote is saved when it finishes (nwos_flush_files()), so that
 * `open(name, "w").write(text)` works as in CPython. Files are at most
 * FFS_MAX_FILE_SIZE bytes.
 * ================================================================ */
#include <string.h>
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/builtin.h"
#include "py/stream.h"
#include "../../../fs/flashfs.h"

#define MAX_OPEN 4

typedef struct {
    mp_obj_base_t base;
    vstr_t        data;
    bool          open;
    bool          dirty;           /* written since the last save */
    char          name[FFS_NAME_LEN];
} nwos_writer_t;

/* Writers that are open, so they can be saved when a script ends (and
 * so the garbage collector keeps them) */
MP_REGISTER_ROOT_POINTER(mp_obj_t nwos_open_files[4]);

static void forget(nwos_writer_t *w) {
    for (int i = 0; i < MAX_OPEN; i++)
        if (MP_STATE_VM(nwos_open_files)[i] == MP_OBJ_FROM_PTR(w))
            MP_STATE_VM(nwos_open_files)[i] = MP_OBJ_NULL;
}

static int save(nwos_writer_t *w) {
    int n = flashfs_write(w->name, w->data.buf, (uint32_t)w->data.len);
    if (n != (int)w->data.len) return MP_ENOSPC;
    w->dirty = false;
    return 0;
}

static mp_uint_t writer_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    nwos_writer_t *w = MP_OBJ_TO_PTR(self_in);
    if (!w->open) { *errcode = MP_EBADF; return MP_STREAM_ERROR; }
    if (w->data.len + size > FFS_MAX_FILE_SIZE) { *errcode = MP_ENOSPC; return MP_STREAM_ERROR; }
    vstr_add_strn(&w->data, buf, size);
    w->dirty = true;
    return size;
}

static mp_uint_t writer_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    (void)arg;
    nwos_writer_t *w = MP_OBJ_TO_PTR(self_in);
    if (request != MP_STREAM_FLUSH && request != MP_STREAM_CLOSE) {
        *errcode = MP_EINVAL;
        return MP_STREAM_ERROR;
    }
    if (!w->open) {
        if (request == MP_STREAM_CLOSE) return 0;       /* closing twice is fine */
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    int e = save(w);
    if (request == MP_STREAM_CLOSE) {
        w->open = false;
        forget(w);
        vstr_clear(&w->data);
    }
    if (e) { *errcode = e; return MP_STREAM_ERROR; }
    return 0;
}

static const mp_rom_map_elem_t writer_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_write),     MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush),     MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_close),     MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__),  MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(writer_locals, writer_locals_table);

static const mp_stream_p_t text_writer_p  = { .write = writer_write, .ioctl = writer_ioctl, .is_text = true };
static const mp_stream_p_t bytes_writer_p = { .write = writer_write, .ioctl = writer_ioctl };

MP_DEFINE_CONST_OBJ_TYPE(nwos_type_textwriter, MP_QSTR_TextIOWrapper, MP_TYPE_FLAG_NONE,
                         protocol, &text_writer_p, locals_dict, &writer_locals);
MP_DEFINE_CONST_OBJ_TYPE(nwos_type_byteswriter, MP_QSTR_FileIO, MP_TYPE_FLAG_NONE,
                         protocol, &bytes_writer_p, locals_dict, &writer_locals);

/* Save what was written to files that are still open, keeping them
 * open; returns how many couldn't be saved */
int nwos_flush_files(void) {
    int failed = 0;
    for (int i = 0; i < MAX_OPEN; i++) {
        mp_obj_t o = MP_STATE_VM(nwos_open_files)[i];
        if (o == MP_OBJ_NULL) continue;
        nwos_writer_t *w = MP_OBJ_TO_PTR(o);
        if (w->dirty && save(w) != 0) failed++;
    }
    return failed;
}

/* Save and close every file a script left open; returns how many of
 * them couldn't be saved */
int nwos_close_files(void) {
    int failed = 0;
    for (int i = 0; i < MAX_OPEN; i++) {
        mp_obj_t o = MP_STATE_VM(nwos_open_files)[i];
        if (o == MP_OBJ_NULL) continue;
        int err = 0;
        if (writer_ioctl(o, MP_STREAM_CLOSE, 0, &err) == MP_STREAM_ERROR) failed++;
        MP_STATE_VM(nwos_open_files)[i] = MP_OBJ_NULL;
    }
    return failed;
}

static mp_obj_t open_for_writing(const char *name, bool append, bool binary) {
    if (strlen(name) >= FFS_NAME_LEN)
        mp_raise_ValueError(MP_ERROR_TEXT("file name too long"));
    int slot = -1;
    for (int i = 0; i < MAX_OPEN && slot < 0; i++)
        if (MP_STATE_VM(nwos_open_files)[i] == MP_OBJ_NULL) slot = i;
    if (slot < 0) mp_raise_OSError(MP_EMFILE);

    nwos_writer_t *w = mp_obj_malloc(nwos_writer_t, binary ? &nwos_type_byteswriter
                                                           : &nwos_type_textwriter);
    vstr_init(&w->data, 64);
    strcpy(w->name, name);
    w->open = true;
    w->dirty = !append;            /* "w" empties the file even if nothing is written */
    uint32_t off, size;
    if (append && flashfs_open_read(name, &off, &size) == 0 && size) {
        char *dst = vstr_add_len(&w->data, size);
        if (flashfs_read(off, dst, size) != (int)size) mp_raise_OSError(MP_EIO);
    }
    MP_STATE_VM(nwos_open_files)[slot] = MP_OBJ_FROM_PTR(w);
    return MP_OBJ_FROM_PTR(w);
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)kwargs;
    const char *name = mp_obj_str_get_str(args[0]);
    const char *mode = n_args > 1 ? mp_obj_str_get_str(args[1]) : "r";
    bool binary = strchr(mode, 'b') != NULL;
    if (!strcmp(mode, "w") || !strcmp(mode, "wt") || !strcmp(mode, "wb"))
        return open_for_writing(name, false, binary);
    if (!strcmp(mode, "a") || !strcmp(mode, "at") || !strcmp(mode, "ab"))
        return open_for_writing(name, true, binary);
    if (strcmp(mode, "r") && strcmp(mode, "rt") && strcmp(mode, "rb"))
        mp_raise_ValueError(MP_ERROR_TEXT("mode must be r, w or a (with b or t)"));

    uint32_t off, size;
    if (flashfs_open_read(name, &off, &size) != 0) mp_raise_OSError(MP_ENOENT);
    vstr_t vstr;
    vstr_init_len(&vstr, size);
    if (flashfs_read(off, vstr.buf, size) != (int)size) {
        vstr_clear(&vstr);
        mp_raise_OSError(MP_EIO);
    }
    if (binary) {
        mp_obj_t data = mp_obj_new_bytes_from_vstr(&vstr);
        return mp_call_function_1(MP_OBJ_FROM_PTR(&mp_type_bytesio), data);
    }
    mp_obj_t text = mp_obj_new_str_from_vstr(&vstr);
    return mp_call_function_1(MP_OBJ_FROM_PTR(&mp_type_stringio), text);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
