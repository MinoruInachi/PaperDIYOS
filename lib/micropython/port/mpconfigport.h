// PaperDIY OS 用の MicroPython 設定。

#include <stdint.h>
#include <alloca.h>
#include <time.h>

#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

#define MICROPY_MPHALPORT_H                     "mphalport.h"
#define MICROPY_HW_BOARD_NAME                   "M5Stack PaperDIY"
#define MICROPY_HW_MCU_NAME                     "ESP32-S3"
#define MICROPY_PY_SYS_PLATFORM                 "esp32"

// Xtensa の窓付き ABI。esp32 ポートと同じく setjmp で例外を飛ばす。
#define MICROPY_NLR_SETJMP                      (1)
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_ENABLE_FINALISER                (1)
#define MICROPY_STACK_CHECK                     (1)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF  (1)
#define MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE    (256)
#define MICROPY_KBD_EXCEPTION                   (1)
#define MICROPY_ENABLE_SCHEDULER                (1)
#define MICROPY_HELPER_REPL                     (1)
#define MICROPY_REPL_AUTO_INDENT                (1)
#define MICROPY_REPL_EMACS_KEYS                 (1)
#define MICROPY_REPL_INFO                       (0)
#define MICROPY_READLINE_HISTORY_SIZE           (16)
#define MICROPY_LONGINT_IMPL                    (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_ERROR_REPORTING                 (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_ROM_TEXT_COMPRESSION            (0)
#define MICROPY_USE_INTERNAL_ERRNO              (1)
#define MICROPY_USE_INTERNAL_PRINTF             (0)
#define MICROPY_PY_STR_BYTES_CMP_WARN           (0)
#define MICROPY_ALLOC_PATH_MAX                  (256)
#define MICROPY_EPOCH_IS_1970                   (1)
#define MICROPY_TIMESTAMP_IMPL                  (MICROPY_TIMESTAMP_IMPL_TIME_T)

// ファイルは SdFat が持っているので、VFS は通さず自前の open / os を置く。
#define MICROPY_VFS                             (0)
#define MICROPY_READER_VFS                      (0)
#define MICROPY_PY_IO_FILEIO                    (0)
#define MICROPY_PY_SYS_STDFILES                 (1)
#define MICROPY_PY_SYS_STDIO_BUFFER             (1)
#define MICROPY_PY_SYS_EXC_INFO                 (1)
#define MICROPY_PY_SYS_EXIT                     (1)
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS       (1)
#define MICROPY_MODULE_FROZEN                   (0)
#define MICROPY_PERSISTENT_CODE_LOAD            (0)
#define MICROPY_EMIT_XTENSAWIN                  (0)
#define MICROPY_ENABLE_VM_ABORT                 (1)

// 組み込むモジュール。生成スクリプトでソースを写したものだけ有効にする。
#define MICROPY_PY_JSON                         (1)
#define MICROPY_PY_RE                           (1)
#define MICROPY_PY_BINASCII                     (1)
#define MICROPY_PY_RANDOM                       (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS           (1)
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC        (pd_cycles())
#define MICROPY_PY_TIME                         (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS            (1)
#define MICROPY_PY_TIME_INCLUDEFILE             "time_port.h"
#define MICROPY_PY_OS                           (0)
#define MICROPY_PY_MACHINE                      (0)
#define MICROPY_PY_ASYNCIO                      (0)
#define MICROPY_PY_UCTYPES                      (0)
#define MICROPY_PY_DEFLATE                      (1)
#define MICROPY_PY_BINASCII_CRC32               (1)
#define MICROPY_PY_HASHLIB                      (0)
#define MICROPY_PY_CRYPTOLIB                    (0)
#define MICROPY_PY_HEAPQ                        (0)
#define MICROPY_PY_SELECT                       (0)
#define MICROPY_PY_PLATFORM                     (0)
#define MICROPY_PY_WEBSOCKET                    (0)
#define MICROPY_PY_WEBREPL                      (0)
#define MICROPY_PY_FRAMEBUF                     (0)
#define MICROPY_PY_BTREE                        (0)
#define MICROPY_PY_SOCKET                       (0)
#define MICROPY_PY_NETWORK                      (0)
#define MICROPY_PY_THREAD                       (0)
#define MICROPY_PY_BLUETOOTH                    (0)
#define MICROPY_PY_ONEWIRE                      (0)
#define MICROPY_PY_LWIP                         (0)
#define MICROPY_PY_SSL                          (0)

typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;
#define MP_SSIZE_MAX                            (0x7fffffff)

// VM が定期的に呼ぶ。USB コンソールを動かし、ボタンの割り込みを受ける。
extern void pd_vm_hook(void);
#define MICROPY_VM_HOOK_COUNT                   (4096)
#define MICROPY_VM_HOOK_INIT                    static unsigned int vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL                    if (--vm_hook_divisor == 0) { vm_hook_divisor = MICROPY_VM_HOOK_COUNT; pd_vm_hook(); }
#define MICROPY_VM_HOOK_LOOP                    MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN                  MICROPY_VM_HOOK_POLL

#define MICROPY_INTERNAL_EVENT_HOOK             pd_vm_hook()
#define MICROPY_INTERNAL_WFE(TIMEOUT_MS)        pd_delay_ms((TIMEOUT_MS) < 0 || (TIMEOUT_MS) > 1 ? 1 : (TIMEOUT_MS))
extern void pd_delay_ms(uint32_t ms);
extern uint32_t pd_cycles(void);

#define MP_STATE_PORT                           MP_STATE_VM
