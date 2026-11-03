// paperdiy モジュール。Python から本体を使うための窓口。
// badger2040 互換層（python/lib/badger2040.py）もこの上に作ってある。

#include <string.h>

#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include "pd_bridge.h"

#define STR(o) mp_obj_str_get_str(o)
#define INT(o) mp_obj_get_int(o)

// --- 描画 ---------------------------------------------------------------------

static mp_obj_t mod_clear(size_t n, const mp_obj_t *a) {
    pd_gfx_clear(n > 0 ? INT(a[0]) : 15);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(clear_obj, 0, 1, mod_clear);

static mp_obj_t mod_set_pen(mp_obj_t pen) {
    pd_gfx_set_pen(INT(pen));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(set_pen_obj, mod_set_pen);

static mp_obj_t mod_pixel(mp_obj_t x, mp_obj_t y) {
    pd_gfx_pixel(INT(x), INT(y));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(pixel_obj, mod_pixel);

static mp_obj_t mod_rect(size_t n, const mp_obj_t *a) {
    pd_gfx_fill(INT(a[0]), INT(a[1]), INT(a[2]), INT(a[3]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rect_obj, 4, 4, mod_rect);

static mp_obj_t mod_frame(size_t n, const mp_obj_t *a) {
    pd_gfx_frame(INT(a[0]), INT(a[1]), INT(a[2]), INT(a[3]), n > 4 ? INT(a[4]) : 2);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(frame_obj, 4, 5, mod_frame);

static mp_obj_t mod_line(size_t n, const mp_obj_t *a) {
    pd_gfx_line(INT(a[0]), INT(a[1]), INT(a[2]), INT(a[3]), n > 4 ? INT(a[4]) : 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(line_obj, 4, 5, mod_line);

static mp_obj_t mod_circle(size_t n, const mp_obj_t *a) {
    pd_gfx_circle(INT(a[0]), INT(a[1]), INT(a[2]), n > 3 ? mp_obj_is_true(a[3]) : true);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(circle_obj, 3, 4, mod_circle);

static mp_obj_t mod_invert(size_t n, const mp_obj_t *a) {
    pd_gfx_invert(INT(a[0]), INT(a[1]), INT(a[2]), INT(a[3]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(invert_obj, 4, 4, mod_invert);

// text(s, x, y, em=28)  y はベースライン。描いた幅を返す。
static mp_obj_t mod_text(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(pd_gfx_text(STR(a[0]), INT(a[1]), INT(a[2]), n > 3 ? INT(a[3]) : 28));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(text_obj, 3, 4, mod_text);

// text_vertical(s, x, y, em=28)  縦に積む。次の y を返す。
static mp_obj_t mod_text_vertical(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(pd_gfx_text_vertical(STR(a[0]), INT(a[1]), INT(a[2]), n > 3 ? INT(a[3]) : 28));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(text_vertical_obj, 3, 4, mod_text_vertical);

static mp_obj_t mod_text_width(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(pd_gfx_text_width(STR(a[0]), n > 1 ? INT(a[1]) : 28));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(text_width_obj, 1, 2, mod_text_width);

// text_wrap(s, x, y, w, em=28, line_height=0, max_lines=0)  次のベースラインを返す。
static mp_obj_t mod_text_wrap(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(pd_gfx_text_wrap(STR(a[0]), INT(a[1]), INT(a[2]), INT(a[3]),
        n > 4 ? INT(a[4]) : 28, n > 5 ? INT(a[5]) : 0, n > 6 ? INT(a[6]) : 0));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(text_wrap_obj, 4, 7, mod_text_wrap);

static mp_obj_t mod_update(size_t n, const mp_obj_t *a) {
    pd_gfx_update(n > 0 ? INT(a[0]) : 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(update_obj, 0, 1, mod_update);

// bitmap(data, w, h, x, y, scale=1)  1bit、MSB 先頭、1 = インク。
static mp_obj_t mod_bitmap(size_t n, const mp_obj_t *a) {
    mp_buffer_info_t b;
    mp_get_buffer_raise(a[0], &b, MP_BUFFER_READ);
    const int w = INT(a[1]), h = INT(a[2]);
    if ((size_t)(((w + 7) / 8) * h) > b.len) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small"));
    }
    pd_gfx_bitmap(b.buf, w, h, INT(a[3]), INT(a[4]), n > 5 ? INT(a[5]) : 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bitmap_obj, 5, 6, mod_bitmap);

// image(path, x=0, y=0, w=0, h=0)  w,h を省くと原寸。指定すると箱に収める。
static mp_obj_t mod_image(size_t n, const mp_obj_t *a) {
    if (!pd_gfx_image(STR(a[0]), n > 1 ? INT(a[1]) : 0, n > 2 ? INT(a[2]) : 0,
        n > 3 ? INT(a[3]) : 0, n > 4 ? INT(a[4]) : 0)) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(image_obj, 1, 5, mod_image);

static mp_obj_t mod_image_size(mp_obj_t path) {
    int w = 0, h = 0;
    if (!pd_gfx_image_size(STR(path), &w, &h)) {
        mp_raise_OSError(MP_ENOENT);
    }
    mp_obj_t t[2] = { MP_OBJ_NEW_SMALL_INT(w), MP_OBJ_NEW_SMALL_INT(h) };
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(image_size_obj, mod_image_size);

static mp_obj_t mod_qr(size_t n, const mp_obj_t *a) {
    return mp_obj_new_int(pd_gfx_qr(STR(a[0]), INT(a[1]), INT(a[2]), INT(a[3])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qr_obj, 4, 4, mod_qr);

// qr_matrix(text) -> (size, bytes)
static mp_obj_t mod_qr_matrix(mp_obj_t text) {
    vstr_t v;
    const size_t cap = 4096;
    vstr_init_len(&v, cap);
    const int side = pd_qr_matrix(STR(text), (uint8_t *)v.buf, cap);
    if (side == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("text too long for QR"));
    }
    v.len = ((side + 7) / 8) * side;
    mp_obj_t t[2] = { MP_OBJ_NEW_SMALL_INT(side), mp_obj_new_bytes_from_vstr(&v) };
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qr_matrix_obj, mod_qr_matrix);

// --- 入力 ---------------------------------------------------------------------

static mp_obj_t mod_key(void) {
    return MP_OBJ_NEW_SMALL_INT(pd_key_poll());
}
static MP_DEFINE_CONST_FUN_OBJ_0(key_obj, mod_key);

// wait_key(timeout_ms=-1)  押されるまで待つ。電池で長く放っておくと眠る
// （起きたらスクリプトの先頭からやり直す。Badger の halt と同じ）。
static mp_obj_t mod_wait_key(size_t n, const mp_obj_t *a) {
    const int timeout = n > 0 ? INT(a[0]) : -1;
    const uint32_t start = pd_millis();
    pd_touch();
    for (;;) {
        const int k = pd_key_poll();
        if (k != 0) {
            pd_touch();
            return MP_OBJ_NEW_SMALL_INT(k);
        }
        if (timeout >= 0 && (int)(pd_millis() - start) >= timeout) {
            return MP_OBJ_NEW_SMALL_INT(0);
        }
        if (timeout < 0) {
            pd_idle_check();
        }
        mp_event_handle_nowait();
        pd_delay_ms(5);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wait_key_obj, 0, 1, mod_wait_key);

static mp_obj_t mod_wake_key(void) {
    return MP_OBJ_NEW_SMALL_INT(pd_wake_key());
}
static MP_DEFINE_CONST_FUN_OBJ_0(wake_key_obj, mod_wake_key);

// capture_back(True)  A 長押しをスクリプトで受ける（KEY_LONG_A / BUTTON_A）。
// ふつうは OS が「戻る」として受け取り、スクリプトを止めてランチャーへ戻る。
// A+B はこの設定によらず常にランチャーへ戻る。
static mp_obj_t mod_capture_back(mp_obj_t on) {
    pd_capture_back(mp_obj_is_true(on));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(capture_back_obj, mod_capture_back);

// --- 本体 ---------------------------------------------------------------------

static mp_obj_t mod_language(void) {
    return mp_obj_new_str(pd_language(), strlen(pd_language()));
}
static MP_DEFINE_CONST_FUN_OBJ_0(language_obj, mod_language);

// battery() -> (mV, 残量%, 外部電源, 充電中)
static mp_obj_t mod_battery(void) {
    int mv = 0, pct = 0;
    bool ext = false, chg = false;
    pd_battery(&mv, &pct, &ext, &chg);
    mp_obj_t t[4] = {
        MP_OBJ_NEW_SMALL_INT(mv), MP_OBJ_NEW_SMALL_INT(pct), mp_obj_new_bool(ext), mp_obj_new_bool(chg),
    };
    return mp_obj_new_tuple(4, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(battery_obj, mod_battery);

static mp_obj_t mod_deep_sleep(size_t n, const mp_obj_t *a) {
    pd_deep_sleep(n > 0 ? INT(a[0]) : 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(deep_sleep_obj, 0, 1, mod_deep_sleep);

static mp_obj_t mod_light_sleep(mp_obj_t sec) {
    pd_light_sleep(INT(sec));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(light_sleep_obj, mod_light_sleep);

static mp_obj_t mod_power_off(void) {
    pd_power_off();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(power_off_obj, mod_power_off);

static mp_obj_t mod_reset(void) {
    pd_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(reset_obj, mod_reset);

// launch(target)  別のアプリへ切り替える。呼ぶとスクリプトは終わる。
// target はネイティブアプリの id か .py のパス。"launcher" でランチャー。
static mp_obj_t mod_launch(mp_obj_t target) {
    pd_launch(STR(target));
    mp_raise_type(&mp_type_SystemExit);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(launch_obj, mod_launch);

// REPL を閉じて元のアプリへ戻る。
static mp_obj_t mod_leave_repl(void) {
    pd_repl_leave();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(leave_repl_obj, mod_leave_repl);

static mp_obj_t mod_set_time(mp_obj_t epoch) {
    pd_set_time(mp_obj_get_int(epoch));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(set_time_obj, mod_set_time);

// --- 無線 ---------------------------------------------------------------------

static mp_obj_t mod_wifi_connect(size_t n, const mp_obj_t *a) {
    if (!pd_net_connect(n > 0 ? INT(a[0]) * 1000 : 15000)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("wifi connect failed"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wifi_connect_obj, 0, 1, mod_wifi_connect);

static mp_obj_t mod_wifi_stop(void) {
    pd_net_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(wifi_stop_obj, mod_wifi_stop);

static mp_obj_t mod_wifi_online(void) {
    return mp_obj_new_bool(pd_net_online());
}
static MP_DEFINE_CONST_FUN_OBJ_0(wifi_online_obj, mod_wifi_online);

static mp_obj_t mod_wifi_ip(void) {
    return mp_obj_new_str(pd_net_address(), strlen(pd_net_address()));
}
static MP_DEFINE_CONST_FUN_OBJ_0(wifi_ip_obj, mod_wifi_ip);

static mp_obj_t mod_ntp_sync(void) {
    return mp_obj_new_bool(pd_net_sync_time());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ntp_sync_obj, mod_ntp_sync);

static bool http_sink(void *user, const char *data, size_t len) {
    vstr_add_strn((vstr_t *)user, data, len);
    return true;
}

// http_get(url) -> (status, bytes)
static mp_obj_t mod_http_get(mp_obj_t url) {
    vstr_t v;
    vstr_init(&v, 1024);
    const int status = pd_http_get(STR(url), http_sink, &v);
    mp_obj_t t[2] = { MP_OBJ_NEW_SMALL_INT(status), mp_obj_new_bytes_from_vstr(&v) };
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(http_get_obj, mod_http_get);

static const mp_rom_map_elem_t paperdiy_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_paperdiy) },
    { MP_ROM_QSTR(MP_QSTR_WIDTH), MP_ROM_INT(540) },
    { MP_ROM_QSTR(MP_QSTR_HEIGHT), MP_ROM_INT(960) },
    { MP_ROM_QSTR(MP_QSTR_KEY_NONE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_KEY_A), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_KEY_B), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_KEY_LONG_A), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_KEY_LONG_B), MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_UPDATE_QUALITY), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_UPDATE_TEXT), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_UPDATE_FAST), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_UPDATE_FASTEST), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pen), MP_ROM_PTR(&set_pen_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_frame), MP_ROM_PTR(&frame_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&line_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_invert), MP_ROM_PTR(&invert_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&text_obj) },
    { MP_ROM_QSTR(MP_QSTR_text_vertical), MP_ROM_PTR(&text_vertical_obj) },
    { MP_ROM_QSTR(MP_QSTR_text_width), MP_ROM_PTR(&text_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_text_wrap), MP_ROM_PTR(&text_wrap_obj) },
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&update_obj) },
    { MP_ROM_QSTR(MP_QSTR_bitmap), MP_ROM_PTR(&bitmap_obj) },
    { MP_ROM_QSTR(MP_QSTR_image), MP_ROM_PTR(&image_obj) },
    { MP_ROM_QSTR(MP_QSTR_image_size), MP_ROM_PTR(&image_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_qr), MP_ROM_PTR(&qr_obj) },
    { MP_ROM_QSTR(MP_QSTR_qr_matrix), MP_ROM_PTR(&qr_matrix_obj) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&key_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait_key), MP_ROM_PTR(&wait_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_key), MP_ROM_PTR(&wake_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_capture_back), MP_ROM_PTR(&capture_back_obj) },
    { MP_ROM_QSTR(MP_QSTR_language), MP_ROM_PTR(&language_obj) },
    { MP_ROM_QSTR(MP_QSTR_battery), MP_ROM_PTR(&battery_obj) },
    { MP_ROM_QSTR(MP_QSTR_deep_sleep), MP_ROM_PTR(&deep_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_light_sleep), MP_ROM_PTR(&light_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_power_off), MP_ROM_PTR(&power_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_launch), MP_ROM_PTR(&launch_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_time), MP_ROM_PTR(&set_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_leave_repl), MP_ROM_PTR(&leave_repl_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_connect), MP_ROM_PTR(&wifi_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_stop), MP_ROM_PTR(&wifi_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_online), MP_ROM_PTR(&wifi_online_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_ip), MP_ROM_PTR(&wifi_ip_obj) },
    { MP_ROM_QSTR(MP_QSTR_ntp_sync), MP_ROM_PTR(&ntp_sync_obj) },
    { MP_ROM_QSTR(MP_QSTR_http_get), MP_ROM_PTR(&http_get_obj) },
};
static MP_DEFINE_CONST_DICT(paperdiy_globals, paperdiy_globals_table);

const mp_obj_module_t pd_module_paperdiy = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&paperdiy_globals,
};
MP_REGISTER_MODULE(MP_QSTR_paperdiy, pd_module_paperdiy);
