// builtins.open と os モジュール。SD カード（SdFat）の上に作る。
// VFS は使わない。FAT のボリュームは SDK の SDCardManager が持っているので、
// MicroPython 側に別のドライバを置くと二重に書き込むことになる。

#include <string.h>

#include "py/builtin.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/stream.h"

#include "pd_bridge.h"
#include "pd_path.h"

// --- パス ---------------------------------------------------------------------

static char s_cwd[128] = "/";

void pd_path_reset(void) {
    strcpy(s_cwd, "/");
}

const char *pd_path_cwd(void) {
    return s_cwd;
}

bool pd_path_resolve(const char *path, char *out, size_t cap) {
    char joined[256];
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(joined)) {
            return false;
        }
        strcpy(joined, path);
    } else {
        size_t a = strlen(s_cwd), b = strlen(path);
        if (a + b + 2 >= sizeof(joined)) {
            return false;
        }
        strcpy(joined, s_cwd);
        if (a > 0 && s_cwd[a - 1] != '/') {
            strcat(joined, "/");
        }
        strcat(joined, path);
    }
    // . と .. を畳む。
    size_t o = 0;
    out[o++] = '/';
    const char *p = joined;
    while (*p != '\0') {
        while (*p == '/') {
            ++p;
        }
        const char *q = p;
        while (*q != '\0' && *q != '/') {
            ++q;
        }
        size_t len = (size_t)(q - p);
        if (len == 0) {
            break;
        }
        if (len == 1 && p[0] == '.') {
            // 何もしない
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            if (o > 1) {
                --o;
                while (o > 1 && out[o - 1] != '/') {
                    --o;
                }
            }
        } else {
            if (o + len + 2 >= cap) {
                return false;
            }
            memcpy(out + o, p, len);
            o += len;
            out[o++] = '/';
        }
        p = q;
    }
    if (o > 1) {
        --o;  // 末尾の / を落とす
    }
    out[o] = '\0';
    return true;
}

bool pd_path_chdir(const char *path) {
    char full[128];
    bool is_dir = true;
    uint64_t size;
    uint32_t mtime;
    if (!pd_path_resolve(path, full, sizeof(full))) {
        return false;
    }
    if (strcmp(full, "/") != 0 && (!pd_fs_stat(full, &is_dir, &size, &mtime) || !is_dir)) {
        return false;
    }
    strcpy(s_cwd, full);
    return true;
}

static void resolve_or_raise(mp_obj_t path_in, char *out, size_t cap) {
    if (!pd_sd_ready()) {
        mp_raise_OSError(MP_ENODEV);
    }
    if (!pd_path_resolve(mp_obj_str_get_str(path_in), out, cap)) {
        mp_raise_OSError(MP_EINVAL);
    }
}

// --- ファイル -----------------------------------------------------------------

typedef struct {
    mp_obj_base_t base;
    void *handle;
} pd_file_obj_t;

static void check_open(pd_file_obj_t *self) {
    if (self->handle == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("I/O operation on closed file"));
    }
}

static void file_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    pd_file_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<io.%q %p>", self->base.type->name, self->handle);
}

static mp_uint_t file_read(mp_obj_t o_in, void *buf, mp_uint_t size, int *errcode) {
    pd_file_obj_t *self = MP_OBJ_TO_PTR(o_in);
    check_open(self);
    int got = pd_fs_read(self->handle, buf, size);
    if (got < 0) {
        *errcode = MP_EIO;
        return MP_STREAM_ERROR;
    }
    return (mp_uint_t)got;
}

static mp_uint_t file_write(mp_obj_t o_in, const void *buf, mp_uint_t size, int *errcode) {
    pd_file_obj_t *self = MP_OBJ_TO_PTR(o_in);
    check_open(self);
    int wrote = pd_fs_write(self->handle, buf, size);
    if (wrote < 0) {
        *errcode = MP_EIO;
        return MP_STREAM_ERROR;
    }
    return (mp_uint_t)wrote;
}

static mp_uint_t file_ioctl(mp_obj_t o_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    pd_file_obj_t *self = MP_OBJ_TO_PTR(o_in);
    if (request != MP_STREAM_CLOSE) {
        check_open(self);
    }
    switch (request) {
        case MP_STREAM_FLUSH:
            pd_fs_flush(self->handle);
            return 0;
        case MP_STREAM_SEEK: {
            struct mp_stream_seek_t *s = (struct mp_stream_seek_t *)arg;
            uint64_t base = 0;
            if (s->whence == MP_SEEK_CUR) {
                base = pd_fs_tell(self->handle);
            } else if (s->whence == MP_SEEK_END) {
                base = pd_fs_size(self->handle);
            }
            if (!pd_fs_seek(self->handle, base + s->offset)) {
                *errcode = MP_EINVAL;
                return MP_STREAM_ERROR;
            }
            s->offset = (mp_off_t)pd_fs_tell(self->handle);
            return 0;
        }
        case MP_STREAM_CLOSE:
            if (self->handle != NULL) {
                pd_fs_close(self->handle);
                self->handle = NULL;
            }
            return 0;
        default:
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
    }
}

static const mp_rom_map_elem_t file_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(file_locals_dict, file_locals_dict_table);

static const mp_stream_p_t fileio_stream_p = {
    .read = file_read,
    .write = file_write,
    .ioctl = file_ioctl,
};

static MP_DEFINE_CONST_OBJ_TYPE(
    pd_type_fileio,
    MP_QSTR_FileIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, file_print,
    protocol, &fileio_stream_p,
    locals_dict, &file_locals_dict
    );

static const mp_stream_p_t textio_stream_p = {
    .read = file_read,
    .write = file_write,
    .ioctl = file_ioctl,
    .is_text = true,
};

static MP_DEFINE_CONST_OBJ_TYPE(
    pd_type_textio,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, file_print,
    protocol, &textio_stream_p,
    locals_dict, &file_locals_dict
    );


static mp_obj_t builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    static const mp_arg_t allowed[] = {
        { MP_QSTR_file, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_mode, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_QSTR(MP_QSTR_r)} },
        { MP_QSTR_buffering, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_encoding, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t a[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, args, kwargs, MP_ARRAY_SIZE(allowed), allowed, a);

    char full[256];
    resolve_or_raise(a[0].u_obj, full, sizeof(full));
    const char *mode = mp_obj_str_get_str(a[1].u_obj);
    int err = MP_ENOENT;
    void *h = pd_fs_open(full, mode, &err);
    if (h == NULL) {
        mp_raise_OSError(err);
    }
    pd_file_obj_t *o = mp_obj_malloc_with_finaliser(pd_file_obj_t,
        strchr(mode, 'b') != NULL ? &pd_type_fileio : &pd_type_textio);
    o->handle = h;
    return MP_OBJ_FROM_PTR(o);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, builtin_open);

// --- os ----------------------------------------------------------------------

static mp_obj_t os_getcwd(void) {
    return mp_obj_new_str(pd_path_cwd(), strlen(pd_path_cwd()));
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_getcwd_obj, os_getcwd);

static mp_obj_t os_chdir(mp_obj_t path_in) {
    if (!pd_path_chdir(mp_obj_str_get_str(path_in))) {
        mp_raise_OSError(MP_ENOENT);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_chdir_obj, os_chdir);

static mp_obj_t list_dir(size_t n_args, const mp_obj_t *args, bool detailed) {
    char full[256];
    if (n_args == 0) {
        strcpy(full, pd_path_cwd());
        if (!pd_sd_ready()) {
            mp_raise_OSError(MP_ENODEV);
        }
    } else {
        resolve_or_raise(args[0], full, sizeof(full));
    }
    void *dir = pd_fs_opendir(full);
    if (dir == NULL) {
        mp_raise_OSError(MP_ENOENT);
    }
    mp_obj_t list = mp_obj_new_list(0, NULL);
    char name[128];
    bool is_dir;
    uint64_t size;
    while (pd_fs_readdir(dir, name, sizeof(name), &is_dir, &size)) {
        mp_obj_t str = mp_obj_new_str(name, strlen(name));
        if (detailed) {
            mp_obj_t t[4] = {
                str,
                MP_OBJ_NEW_SMALL_INT(is_dir ? 0x4000 : 0x8000),
                MP_OBJ_NEW_SMALL_INT(0),
                mp_obj_new_int_from_ull(size),
            };
            mp_obj_list_append(list, mp_obj_new_tuple(4, t));
        } else {
            mp_obj_list_append(list, str);
        }
    }
    pd_fs_closedir(dir);
    return list;
}

static mp_obj_t os_listdir(size_t n_args, const mp_obj_t *args) {
    return list_dir(n_args, args, false);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_listdir_obj, 0, 1, os_listdir);

static mp_obj_t os_ilistdir(size_t n_args, const mp_obj_t *args) {
    return mp_getiter(list_dir(n_args, args, true), NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_ilistdir_obj, 0, 1, os_ilistdir);

static mp_obj_t os_stat(mp_obj_t path_in) {
    char full[256];
    resolve_or_raise(path_in, full, sizeof(full));
    bool is_dir = true;
    uint64_t size = 0;
    uint32_t mtime = 0;
    if (strcmp(full, "/") != 0 && !pd_fs_stat(full, &is_dir, &size, &mtime)) {
        mp_raise_OSError(MP_ENOENT);
    }
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
    t->items[0] = MP_OBJ_NEW_SMALL_INT(is_dir ? 0x4000 : 0x8000);
    for (int i = 1; i < 10; ++i) {
        t->items[i] = MP_OBJ_NEW_SMALL_INT(0);
    }
    t->items[6] = mp_obj_new_int_from_ull(size);
    t->items[7] = t->items[8] = t->items[9] = mp_obj_new_int_from_uint(mtime);
    return MP_OBJ_FROM_PTR(t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_stat_obj, os_stat);

static mp_obj_t os_statvfs(mp_obj_t path_in) {
    (void)path_in;
    uint64_t total = 0, used = 0;
    if (pd_sd_ready()) {
        pd_disk_usage(&total, &used);
    }
    const uint32_t block = 4096;
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
    t->items[0] = MP_OBJ_NEW_SMALL_INT(block);
    t->items[1] = MP_OBJ_NEW_SMALL_INT(block);
    t->items[2] = mp_obj_new_int_from_ull(total / block);
    t->items[3] = mp_obj_new_int_from_ull((total - used) / block);
    t->items[4] = t->items[3];
    t->items[5] = t->items[6] = t->items[7] = t->items[8] = MP_OBJ_NEW_SMALL_INT(0);
    t->items[9] = MP_OBJ_NEW_SMALL_INT(255);
    return MP_OBJ_FROM_PTR(t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_statvfs_obj, os_statvfs);

static mp_obj_t path_op(mp_obj_t path_in, bool (*op)(const char *)) {
    char full[256];
    resolve_or_raise(path_in, full, sizeof(full));
    if (!op(full)) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}

static mp_obj_t os_mkdir(mp_obj_t p) { return path_op(p, pd_fs_mkdir); }
static MP_DEFINE_CONST_FUN_OBJ_1(os_mkdir_obj, os_mkdir);
static mp_obj_t os_rmdir(mp_obj_t p) { return path_op(p, pd_fs_rmdir); }
static MP_DEFINE_CONST_FUN_OBJ_1(os_rmdir_obj, os_rmdir);

static mp_obj_t os_remove(mp_obj_t path_in) {
    char full[256];
    resolve_or_raise(path_in, full, sizeof(full));
    bool is_dir;
    uint64_t size;
    uint32_t mtime;
    if (!pd_fs_stat(full, &is_dir, &size, &mtime)) {
        mp_raise_OSError(MP_ENOENT);
    }
    if (!pd_fs_remove(full)) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_remove_obj, os_remove);

static mp_obj_t os_rename(mp_obj_t from_in, mp_obj_t to_in) {
    char from[256], to[256];
    resolve_or_raise(from_in, from, sizeof(from));
    resolve_or_raise(to_in, to, sizeof(to));
    if (!pd_fs_rename(from, to)) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(os_rename_obj, os_rename);

static mp_obj_t os_sync(void) {
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_sync_obj, os_sync);

static mp_obj_t os_urandom(mp_obj_t n_in) {
    size_t n = mp_obj_get_int(n_in);
    vstr_t vstr;
    vstr_init_len(&vstr, n);
    for (size_t i = 0; i < n; ++i) {
        vstr.buf[i] = (char)(pd_cycles() >> ((i % 4) * 8));
    }
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_urandom_obj, os_urandom);

static mp_obj_t os_uname(void) {
    static const qstr fields[5] = {
        MP_QSTR_sysname, MP_QSTR_nodename, MP_QSTR_release, MP_QSTR_version, MP_QSTR_machine,
    };
    mp_obj_t items[5];
    items[0] = items[1] = MP_OBJ_NEW_QSTR(MP_QSTR_esp32);
    items[2] = mp_obj_new_str(pd_py_version(), strlen(pd_py_version()));
    items[3] = items[2];
    items[4] = mp_obj_new_str(MICROPY_HW_BOARD_NAME " with " MICROPY_HW_MCU_NAME,
        strlen(MICROPY_HW_BOARD_NAME " with " MICROPY_HW_MCU_NAME));
    return mp_obj_new_attrtuple(fields, 5, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_uname_obj, os_uname);

static const mp_rom_map_elem_t os_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_uname), MP_ROM_PTR(&os_uname_obj) },
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_os) },
    { MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&os_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&os_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&os_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_ilistdir), MP_ROM_PTR(&os_ilistdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&os_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_statvfs), MP_ROM_PTR(&os_statvfs_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&os_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&os_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&os_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_unlink), MP_ROM_PTR(&os_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&os_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_sync), MP_ROM_PTR(&os_sync_obj) },
    { MP_ROM_QSTR(MP_QSTR_urandom), MP_ROM_PTR(&os_urandom_obj) },
    { MP_ROM_QSTR(MP_QSTR_sep), MP_ROM_QSTR(MP_QSTR__slash_) },
};
static MP_DEFINE_CONST_DICT(os_module_globals, os_module_globals_table);

const mp_obj_module_t pd_module_os = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&os_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_os, pd_module_os);
