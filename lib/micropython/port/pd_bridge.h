#pragma once

// MicroPython（C）と PaperDIY OS（C++）の橋渡し。
// C のモジュールは Arduino を見ないので、ホストでの QSTR 走査にも通る。
// 実装は src/python/PyBridge.cpp。

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- 描画 --------------------------------------------------------------------
void pd_gfx_clear(int pen);
void pd_gfx_set_pen(int pen);
void pd_gfx_pixel(int x, int y);
void pd_gfx_fill(int x, int y, int w, int h);
void pd_gfx_frame(int x, int y, int w, int h, int thickness);
void pd_gfx_line(int x0, int y0, int x1, int y1, int thickness);
void pd_gfx_circle(int x, int y, int r, bool filled);
void pd_gfx_invert(int x, int y, int w, int h);
void pd_gfx_bitmap(const uint8_t *bits, int w, int h, int x, int y, int scale);
int pd_gfx_text(const char *s, int x, int baseline, int em);
int pd_gfx_text_vertical(const char *s, int x, int y, int em);
int pd_gfx_text_width(const char *s, int em);
int pd_gfx_text_wrap(const char *s, int x, int baseline, int w, int em, int line_height, int max_lines);
void pd_gfx_update(int mode);
bool pd_gfx_image(const char *path, int x, int y, int w, int h);
bool pd_gfx_image_size(const char *path, int *w, int *h);
int pd_gfx_qr(const char *text, int x, int y, int size);
// 行ごとにビットを詰めた QR の行列（MSB 先頭、1 = 黒）。一辺を返す。作れなければ 0。
int pd_qr_matrix(const char *text, uint8_t *out, size_t cap);
int pd_width(void);
int pd_height(void);

// --- 入力と電源 --------------------------------------------------------------
// キーの値は pdos::Key と同じ（0 = なし、1 = A、2 = B、3 = 長押し A、4 = 長押し B）。
int pd_key_poll(void);
int pd_wake_key(void);
void pd_capture_back(bool captured);
void pd_touch(void);
// 無操作がつづいて電池駆動なら deep sleep へ入る（戻らない）。
void pd_idle_check(void);
void pd_deep_sleep(uint32_t seconds);
void pd_light_sleep(uint32_t seconds);
void pd_power_off(void);
void pd_reset(void);
void pd_launch(const char *target);
void pd_set_time(int64_t epoch);

const char *pd_language(void);
void pd_battery(int *mv, int *percent, bool *external, bool *charging);
bool pd_sd_ready(void);
void pd_disk_usage(uint64_t *total, uint64_t *used);

// --- 無線 --------------------------------------------------------------------
bool pd_net_connect(uint32_t timeout_ms);
void pd_net_stop(void);
bool pd_net_online(void);
const char *pd_net_address(void);
const char *pd_net_ssid(void);
const char *pd_net_error(void);
bool pd_net_sync_time(void);
typedef bool (*pd_http_sink_t)(void *user, const char *data, size_t len);
int pd_http_get(const char *url, pd_http_sink_t sink, void *user);

// --- ファイル ----------------------------------------------------------------
void *pd_fs_open(const char *path, const char *mode, int *err);
int pd_fs_read(void *h, void *buf, size_t len);
int pd_fs_write(void *h, const void *buf, size_t len);
bool pd_fs_seek(void *h, uint64_t pos);
uint64_t pd_fs_tell(void *h);
uint64_t pd_fs_size(void *h);
void pd_fs_flush(void *h);
void pd_fs_close(void *h);
bool pd_fs_stat(const char *path, bool *is_dir, uint64_t *size, uint32_t *mtime);
bool pd_fs_mkdir(const char *path);
bool pd_fs_rmdir(const char *path);
bool pd_fs_remove(const char *path);
bool pd_fs_rename(const char *from, const char *to);
void *pd_fs_opendir(const char *path);
// 次の項目の名前を name に書く。終わりなら false。
bool pd_fs_readdir(void *dir, char *name, size_t cap, bool *is_dir, uint64_t *size);
void pd_fs_closedir(void *dir);

// 本体に内蔵した .py（python/lib）。無ければ false。
bool pd_embedded_find(const char *name, const char **data, size_t *len);

// --- USB ---------------------------------------------------------------------
int pd_usb_read(void);
int pd_usb_available(void);
void pd_usb_write(const char *data, size_t len);
// REPL を閉じる合図（A+B か USB 抜去）が出ているか。
bool pd_repl_exit_requested(void);
void pd_repl_leave(void);

// --- 時間 --------------------------------------------------------------------
uint32_t pd_millis(void);
uint32_t pd_micros(void);
uint32_t pd_cycles(void);
void pd_delay_ms(uint32_t ms);
void pd_delay_us(uint32_t us);
void pd_background(void);

// --- ランタイム（port/pd_port.c） ---------------------------------------------
enum { PD_RUN_OK = 0, PD_RUN_INTERRUPTED = 1, PD_RUN_ERROR = 2 };
// heap を使って走らせる。エラーならトレースバックを error へ入れる。
int pd_py_run_file(const char *path, void *heap, size_t heap_size, char *error, size_t error_cap);
void pd_py_run_repl(void *heap, size_t heap_size);
void pd_py_interrupt(void);
void pd_py_abort(void);
const char *pd_py_version(void);

#ifdef __cplusplus
}
#endif
