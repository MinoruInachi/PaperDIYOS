// MicroPython（C）から OS（C++）を呼ぶための橋。pd_bridge.h の実装。

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardPaperDiy.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lgfx/utility/lgfx_qrcode.h>
#include <string.h>
#include <sys/time.h>
#include <xtensa/core-macros.h>

#include <pd_bridge.h>

#include "Embedded.h"
#include "PythonApp.h"
#include "os/Console.h"
#include "os/I18n.h"
#include "os/Net.h"
#include "os/Os.h"
#include "os/Ui.h"
#include "pd_interrupt.h"
#include "reader/Http.h"

using namespace pdos;

extern "C" int mp_interrupt_char;
extern "C" void mp_sched_keyboard_interrupt(void);

namespace pdos {

void pyInterrupt() { mp_sched_keyboard_interrupt(); }

void pyInterruptIfRunning() {
  if (mp_interrupt_char >= 0) mp_sched_keyboard_interrupt();
}

int pyInterruptChar() { return mp_interrupt_char; }

}  // namespace pdos

namespace {

Gfx& g() { return os.gfx(); }
SDCardManager& sd() { return SDCardManager::getInstance(); }

// ファイルの持ち手。FsFile は大きいので PSRAM に置かず、普通のヒープへ。
struct Handle {
  FsFile file;
};

// ファイル名のタイムスタンプを Unix 時刻へ。分からなければ 0。
uint32_t fatToUnix(uint16_t date, uint16_t time16) {
  if (date == 0) return 0;
  struct tm t = {};
  t.tm_year = ((date >> 9) & 0x7F) + 80;
  t.tm_mon = ((date >> 5) & 0x0F) - 1;
  t.tm_mday = date & 0x1F;
  t.tm_hour = (time16 >> 11) & 0x1F;
  t.tm_min = (time16 >> 5) & 0x3F;
  t.tm_sec = (time16 & 0x1F) * 2;
  return static_cast<uint32_t>(mktime(&t));
}

}  // namespace

extern "C" {

// --- 描画 --------------------------------------------------------------------

void pd_gfx_clear(int pen) { g().clear(static_cast<uint8_t>(pen)); }
void pd_gfx_set_pen(int pen) { g().setPen(static_cast<uint8_t>(pen)); }
void pd_gfx_pixel(int x, int y) { g().pixel(x, y); }
void pd_gfx_fill(int x, int y, int w, int h) { g().fill(x, y, w, h); }
void pd_gfx_frame(int x, int y, int w, int h, int t) { g().frame(x, y, w, h, t); }
void pd_gfx_line(int x0, int y0, int x1, int y1, int t) { g().line(x0, y0, x1, y1, t); }
void pd_gfx_circle(int x, int y, int r, bool filled) { g().circle(x, y, r, filled); }
void pd_gfx_invert(int x, int y, int w, int h) { g().invert(x, y, w, h); }
void pd_gfx_bitmap(const uint8_t* bits, int w, int h, int x, int y, int scale) { g().bitmap(bits, w, h, x, y, scale); }
int pd_gfx_text(const char* s, int x, int y, int em) { return g().text(s, x, y, em); }
int pd_gfx_text_vertical(const char* s, int x, int y, int em) { return g().textVertical(s, x, y, em); }
int pd_gfx_text_width(const char* s, int em) { return g().textWidth(s, em); }
int pd_gfx_text_wrap(const char* s, int x, int y, int w, int em, int lh, int maxLines) {
  return g().textWrap(s, x, y, w, em, lh, maxLines);
}

void pd_gfx_update(int mode) {
  g().update(static_cast<Update>(mode < 0 || mode > 3 ? 1 : mode));
  os.touch();
}

bool pd_gfx_image(const char* path, int x, int y, int w, int h) {
  int16_t iw = static_cast<int16_t>(w), ih = static_cast<int16_t>(h);
  if (iw <= 0 || ih <= 0) {
    if (!ui::imageSize(path, &iw, &ih)) return false;
  }
  uint8_t* pixels = ui::decodeImage(path, iw, ih);
  if (pixels == nullptr) return false;
  g().gray(pixels, iw, ih, static_cast<int16_t>(x), static_cast<int16_t>(y));
  heap_caps_free(pixels);
  return true;
}

bool pd_gfx_image_size(const char* path, int* w, int* h) {
  int16_t iw = 0, ih = 0;
  if (!ui::imageSize(path, &iw, &ih)) return false;
  *w = iw;
  *h = ih;
  return true;
}

int pd_gfx_qr(const char* text, int x, int y, int size) { return ui::qr(text, x, y, size); }

int pd_qr_matrix(const char* text, uint8_t* out, size_t cap) {
  for (uint8_t version = 1; version <= 25; ++version) {
    const uint16_t bytes = lgfx_qrcode_getBufferSize(version);
    uint8_t* modules = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (modules == nullptr) return 0;
    QRCode code;
    if (lgfx_qrcode_initText(&code, modules, version, ECC_LOW, text) != 0) {
      heap_caps_free(modules);
      continue;
    }
    const int side = code.size;
    const size_t rowBytes = (side + 7) / 8;
    if (rowBytes * side > cap) {
      heap_caps_free(modules);
      return 0;
    }
    memset(out, 0, rowBytes * side);
    for (int yy = 0; yy < side; ++yy) {
      for (int xx = 0; xx < side; ++xx) {
        if (lgfx_qrcode_getModule(&code, xx, yy)) out[yy * rowBytes + xx / 8] |= 0x80 >> (xx % 8);
      }
    }
    heap_caps_free(modules);
    return side;
  }
  return 0;
}

int pd_width(void) { return Gfx::W; }
int pd_height(void) { return Gfx::H; }

// --- 入力と電源 --------------------------------------------------------------

int pd_key_poll(void) { return static_cast<int>(os.pollKey()); }
int pd_wake_key(void) { return static_cast<int>(os.wakeKey()); }
void pd_capture_back(bool captured) { os.setBackCaptured(captured); }
void pd_touch(void) { os.touch(); }
void pd_idle_check(void) { os.idleCheck(); }
void pd_deep_sleep(uint32_t seconds) { os.deepSleepNow(seconds); }
void pd_light_sleep(uint32_t seconds) { os.lightSleepFor(seconds); }
void pd_power_off(void) { os.powerOff(); }
void pd_reset(void) { ESP.restart(); }
void pd_launch(const char* target) {
  char id[96];
  const size_t len = strlen(target);
  if (len > 3 && strcmp(target + len - 3, ".py") == 0) {
    snprintf(id, sizeof(id), "py:%s", target);
  } else {
    snprintf(id, sizeof(id), "%s", target);
  }
  os.setPendingLaunch(id);
}

void pd_set_time(int64_t epoch) {
  struct timeval tv = {static_cast<time_t>(epoch), 0};
  settimeofday(&tv, nullptr);
}

const char* pd_language(void) { return i18n::lang() == i18n::Lang::Ja ? "ja" : "en"; }

void pd_battery(int* mv, int* percent, bool* external, bool* charging) {
  const auto s = os.battery().readStatus();
  *mv = s.millivolts;
  *percent = s.percentage;
  *external = s.externalPower;
  *charging = s.externalPower && BoardPaperDiy::isCharging();
}

bool pd_sd_ready(void) { return os.sdReady(); }

void pd_disk_usage(uint64_t* total, uint64_t* used) {
  *total = sd().sdTotalBytes();
  *used = sd().sdUsedBytes();
}

// --- 無線 --------------------------------------------------------------------

bool pd_net_connect(uint32_t timeout_ms) { return net::connect(timeout_ms); }
void pd_net_stop(void) { net::stop(); }
bool pd_net_online(void) { return net::online(); }
const char* pd_net_address(void) { return net::address(); }
const char* pd_net_ssid(void) { return net::ssid(); }
const char* pd_net_error(void) { return net::lastError(); }
bool pd_net_sync_time(void) { return net::syncTime(); }

int pd_http_get(const char* url, pd_http_sink_t sink, void* user) {
  const paperdiy::HttpResult r = paperdiy::httpGet(url, sink, user);
  return r.status;
}

// --- ファイル ----------------------------------------------------------------

void* pd_fs_open(const char* path, const char* mode, int* err) {
  oflag_t flags = O_RDONLY;
  const bool plus = strchr(mode, '+') != nullptr;
  if (strchr(mode, 'w') != nullptr) {
    flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
  } else if (strchr(mode, 'a') != nullptr) {
    flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
  } else if (strchr(mode, 'x') != nullptr) {
    flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_EXCL;
  } else if (plus) {
    flags = O_RDWR;
  }
  Handle* h = new Handle();
  h->file = sd().open(path, flags);
  if (!h->file) {
    delete h;
    *err = 2;  // ENOENT
    return nullptr;
  }
  if (h->file.isDirectory()) {
    h->file.close();
    delete h;
    *err = 21;  // EISDIR
    return nullptr;
  }
  return h;
}

int pd_fs_read(void* h, void* buf, size_t len) { return static_cast<Handle*>(h)->file.read(buf, len); }
int pd_fs_write(void* h, const void* buf, size_t len) {
  const size_t n = static_cast<Handle*>(h)->file.write(static_cast<const uint8_t*>(buf), len);
  return n == len ? static_cast<int>(n) : -1;
}
bool pd_fs_seek(void* h, uint64_t pos) { return static_cast<Handle*>(h)->file.seekSet(pos); }
uint64_t pd_fs_tell(void* h) { return static_cast<Handle*>(h)->file.curPosition(); }
uint64_t pd_fs_size(void* h) { return static_cast<Handle*>(h)->file.fileSize(); }
void pd_fs_flush(void* h) { static_cast<Handle*>(h)->file.flush(); }

void pd_fs_close(void* h) {
  auto* handle = static_cast<Handle*>(h);
  handle->file.close();
  delete handle;
}

bool pd_fs_stat(const char* path, bool* is_dir, uint64_t* size, uint32_t* mtime) {
  if (strcmp(path, "/") == 0) {
    *is_dir = true;
    *size = 0;
    *mtime = 0;
    return true;
  }
  FsFile f = sd().open(path, O_RDONLY);
  if (!f) return false;
  *is_dir = f.isDirectory();
  *size = f.fileSize();
  uint16_t date = 0, time16 = 0;
  f.getModifyDateTime(&date, &time16);
  *mtime = fatToUnix(date, time16);
  f.close();
  return true;
}

bool pd_fs_mkdir(const char* path) { return sd().mkdir(path, false); }
bool pd_fs_rmdir(const char* path) { return sd().removeDir(path); }
bool pd_fs_remove(const char* path) { return sd().remove(path); }
bool pd_fs_rename(const char* from, const char* to) { return sd().rename(from, to); }

void* pd_fs_opendir(const char* path) {
  Handle* h = new Handle();
  h->file = sd().open(path, O_RDONLY);
  if (!h->file || !h->file.isDirectory()) {
    delete h;
    return nullptr;
  }
  return h;
}

bool pd_fs_readdir(void* dir, char* name, size_t cap, bool* is_dir, uint64_t* size) {
  FsFile entry;
  if (!entry.openNext(&static_cast<Handle*>(dir)->file, O_RDONLY)) return false;
  entry.getName(name, cap);
  *is_dir = entry.isDirectory();
  *size = entry.fileSize();
  entry.close();
  return true;
}

void pd_fs_closedir(void* dir) {
  auto* h = static_cast<Handle*>(dir);
  h->file.close();
  delete h;
}

bool pd_embedded_find(const char* name, const char** data, size_t* len) {
  for (size_t i = 0; i < python::kLibCount; ++i) {
    if (strcmp(python::kLib[i].name, name) == 0) {
      *data = python::kLib[i].data;
      *len = python::kLib[i].size;
      return true;
    }
  }
  return false;
}

// --- USB ---------------------------------------------------------------------

int pd_usb_read(void) { return os.usbRead(); }
int pd_usb_available(void) { return os.usbAvailable(); }

void pd_usb_write(const char* data, size_t len) {
  if (Serial) Serial.write(reinterpret_cast<const uint8_t*>(data), len);
}

bool pd_repl_exit_requested(void) { return os.replExitRequested(); }
void pd_repl_leave(void) { os.leaveRepl(); }

// --- 時間 --------------------------------------------------------------------

uint32_t pd_millis(void) { return millis(); }
uint32_t pd_micros(void) { return micros(); }
uint32_t pd_cycles(void) { return ESP.getCycleCount(); }
void pd_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms == 0 ? 1 : ms)); }
void pd_delay_us(uint32_t us) { delayMicroseconds(us); }

void pd_background(void) {
  os.repeatStopRequest();
  // REPL のあいだ、USB は MicroPython のもの。
  if (!os.inRepl()) console::poll();
}

}  // extern "C"
