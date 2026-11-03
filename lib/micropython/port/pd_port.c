// MicroPython のポート層。GC、例外の受け口、stdio、import、起動と終了。

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/mpprint.h"
#include "py/mpstate.h"
#include "py/obj.h"
#include "py/parse.h"
#include "py/persistentcode.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "shared/readline/readline.h"
#include "shared/runtime/pyexec.h"
#include "genhdr/mpversion.h"

#include <esp_cpu.h>
#include <xtensa/config/core-isa.h>

#include "pd_bridge.h"
#include "pd_path.h"

#define PD_STACK_SIZE (36 * 1024)

// --- GC ---------------------------------------------------------------------

// 窓レジスタの中身をスタックへ追い出すために深く潜ってから走査する（esp32 ポートと同じ）。
static void gc_collect_inner(volatile unsigned int level) {
    if (level < XCHAL_NUM_AREGS / 8) {
        gc_collect_inner(level + 1);
    } else {
        volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();
        gc_collect_root((void **)sp, ((mp_uint_t)MP_STATE_THREAD(stack_top) - sp) / sizeof(uint32_t));
    }
}

void gc_collect(void) {
    gc_collect_start();
    gc_collect_inner(0);
    gc_collect_end();
}

void nlr_jump_fail(void *val) {
    printf("[py] uncaught NLR %p\n", val);
    pd_reset();
    for (;;) {
    }
}

#if MICROPY_DEBUG_PRINTERS || !defined(NDEBUG)
void MP_NORETURN __fatal_error(const char *msg) {
    printf("[py] fatal: %s\n", msg);
    pd_reset();
    for (;;) {
    }
}
#endif

// --- mphal ------------------------------------------------------------------

mp_uint_t mp_hal_ticks_ms(void) { return pd_millis(); }
mp_uint_t mp_hal_ticks_us(void) { return pd_micros(); }
mp_uint_t mp_hal_ticks_cpu(void) { return pd_cycles(); }

uint64_t mp_hal_time_ns(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    uint32_t start = pd_millis();
    while (pd_millis() - start < ms) {
        mp_event_handle_nowait();
        pd_delay_ms(1);
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    if (us >= 1000) {
        mp_hal_delay_ms(us / 1000);
        us %= 1000;
    }
    pd_delay_us(us);
}

void pd_vm_hook(void) {
    pd_background();
}

// 上の段が閉じる合図を出したら、まず行を捨て（Ctrl-C）、次に抜ける（Ctrl-D）。
int mp_hal_stdin_rx_chr(void) {
    static bool sent_clear = false;
    for (;;) {
        if (pd_repl_exit_requested()) {
            if (!sent_clear) {
                sent_clear = true;
                return CHAR_CTRL_C;
            }
            sent_clear = false;
            return CHAR_CTRL_D;
        }
        int c = pd_usb_read();
        if (c >= 0) {
            sent_clear = false;
            return c;
        }
        mp_event_handle_nowait();
        pd_delay_ms(1);
    }
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    pd_usb_write(str, len);
    return len;
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if ((poll_flags & MP_STREAM_POLL_RD) && pd_usb_available() > 0) {
        ret |= MP_STREAM_POLL_RD;
    }
    if (poll_flags & MP_STREAM_POLL_WR) {
        ret |= MP_STREAM_POLL_WR;
    }
    return ret;
}

// --- import -----------------------------------------------------------------

mp_import_stat_t mp_import_stat(const char *path) {
    char full[MICROPY_ALLOC_PATH_MAX];
    if (pd_path_resolve(path, full, sizeof(full)) && pd_sd_ready()) {
        bool is_dir;
        uint64_t size;
        uint32_t mtime;
        if (pd_fs_stat(full, &is_dir, &size, &mtime)) {
            return is_dir ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_FILE;
        }
    }
    const char *data;
    size_t len;
    if (pd_embedded_find(path, &data, &len)) {
        return MP_IMPORT_STAT_FILE;
    }
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    const char *name = qstr_str(filename);
    char full[MICROPY_ALLOC_PATH_MAX];
    int err = 0;
    void *h = NULL;
    if (pd_path_resolve(name, full, sizeof(full)) && pd_sd_ready()) {
        h = pd_fs_open(full, "r", &err);
    }
    if (h == NULL) {
        const char *data;
        size_t len;
        if (pd_embedded_find(name, &data, &len)) {
            return mp_lexer_new_from_str_len(filename, data, len, 0);
        }
        mp_raise_OSError(MP_ENOENT);
    }
    size_t size = (size_t)pd_fs_size(h);
    char *buf = m_new(char, size + 1);
    int got = pd_fs_read(h, buf, size);
    pd_fs_close(h);
    if (got < 0) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_lexer_new_from_str_len(filename, buf, (size_t)got, size + 1);
}

// --- 起動と終了 --------------------------------------------------------------

// stack_top は呼び出し側の変数。GC はここから下を走査するので、Python の
// オブジェクトを持つ変数はすべてこれより深いフレームに置くこと。
static void runtime_init(void *stack_top, void *heap, size_t heap_size) {
    mp_cstack_init_with_top(stack_top, PD_STACK_SIZE);
    gc_init(heap, (uint8_t *)heap + heap_size);
    mp_init();
    pd_path_reset();
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/lib")));
    mp_hal_set_interrupt_char(CHAR_CTRL_C);
}

static void runtime_deinit(void) {
    mp_hal_set_interrupt_char(-1);
    gc_sweep_all();  // 開いたままのファイルを閉じる（__del__）
    mp_deinit();
}

static void MP_NOINLINE run_file_inner(const char *path) {
    mp_lexer_t *lex = mp_lexer_new_from_file(qstr_from_str(path));
    qstr source_name = lex->source_name;
    mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
    mp_obj_t fun = mp_compile(&tree, source_name, false);
    mp_call_function_0(fun);
    mp_handle_pending(true);
}

int pd_py_run_file(const char *path, void *heap, size_t heap_size, char *error, size_t error_cap) {
    volatile int stack_top = 0;
    runtime_init((void *)&stack_top, heap, heap_size);
    int rc = PD_RUN_OK;
    nlr_buf_t nlr;
    nlr.ret_val = NULL;
    if (nlr_push(&nlr) == 0) {
        nlr_set_abort(&nlr);
        run_file_inner(path);
        nlr_pop();
    } else if (nlr.ret_val == NULL) {
        // pd_py_abort で止められた。例外を握りつぶすスクリプトもここへ来る。
        rc = PD_RUN_INTERRUPTED;
    } else {
        mp_obj_base_t *exc = (mp_obj_base_t *)nlr.ret_val;
        if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(exc->type), MP_OBJ_FROM_PTR(&mp_type_KeyboardInterrupt))) {
            rc = PD_RUN_INTERRUPTED;
        } else if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(exc->type), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
            rc = PD_RUN_OK;
        } else {
            rc = PD_RUN_ERROR;
            mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(exc));
            // 画面に出すぶん。固定の箱へ書き、あふれたら切る。
            vstr_t vstr;
            vstr_init_fixed_buf(&vstr, error_cap - 1, error);
            mp_print_t print = { &vstr, (mp_print_strn_t)vstr_add_strn };
            mp_obj_print_exception(&print, MP_OBJ_FROM_PTR(exc));
            error[vstr.len] = '\0';
        }
    }
    nlr_set_abort(NULL);
    runtime_deinit();
    return rc;
}

void pd_py_run_repl(void *heap, size_t heap_size) {
    volatile int stack_top = 0;
    for (;;) {
        runtime_init((void *)&stack_top, heap, heap_size);
        readline_init0();
        // プロンプトで待っているあいだは例外の受け口が無いので、Ctrl-C を割り込みに
        // しない（ここで KeyboardInterrupt が飛ぶと nlr_jump_fail でリセットになる）。
        // コードを実行するときは pyexec が有効にする。
        mp_hal_set_interrupt_char(-1);
        int ret = 0;
        for (;;) {
            if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
                ret = pyexec_raw_repl();
            } else {
                ret = pyexec_friendly_repl();
            }
            if (ret != 0) {
                break;
            }
        }
        const bool leaving = pd_repl_exit_requested();
        if (!leaving) {
            mp_hal_stdout_tx_str("MPY: soft reboot\r\n");
        }
        runtime_deinit();
        if (leaving) {
            pyexec_mode_kind = PYEXEC_MODE_FRIENDLY_REPL;
            return;
        }
    }
}

// 別のタスクから呼ばれる。実行中のコードに KeyboardInterrupt を起こす。
void pd_py_interrupt(void) {
    mp_sched_keyboard_interrupt();
}

// 捕まえられない中断。KeyboardInterrupt で止まらないスクリプト用。
void pd_py_abort(void) {
    if (nlr_get_abort() != NULL) {
        mp_sched_vm_abort();
    }
}

const char *pd_py_version(void) {
    return MICROPY_VERSION_STRING;
}
