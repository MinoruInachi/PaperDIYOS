// 時計。Badger の clock.py に当たる。
//
// PaperDIY には RTC チップが無い（回路図で確認）。時刻は ESP32-S3 内蔵の RTC
// タイマーで数えているので、deep sleep とリセットは越えるが、電源を切ると失う。
// 内蔵の発振器は精度が低いので、NTP で合わせ直す前提にする。
//
// 毎分描き直すので、deep sleep ではなく light sleep で待つ（deep sleep だと
// 起きるたびに全画面が点滅する）。

#include <Arduino.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "os/I18n.h"
#include "Apps.h"
#include "os/Icons.h"
#include "os/Net.h"
#include "os/Ui.h"

namespace pdos {
namespace {

constexpr int kCleanEveryMinutes = 60;

class ClockApp : public App {
 public:
  const char* id() const override { return "clock"; }
  const char* name() const override { return TR("時計", "Clock"); }
  const uint8_t* icon() const override { return icons::clock; }

  void start(bool resumed, Key) override {
    setting_ = false;
    (void)resumed;
    if (net::syncDue()) sync();
    render(Update::Text);
    armNextMinute();
  }

  void key(Key k) override {
    if (setting_) {
      settingKey(k);
      return;
    }
    switch (k) {
      case Key::A:
        beginSetting();
        break;
      case Key::B:
        render(Update::Text);
        break;
      case Key::LongB:
        sync();
        render(Update::Text);
        armNextMinute();
        break;
      case Key::LongA:
        os.goHome();
        break;
      default:
        break;
    }
  }

  void tick() override {
    if (setting_) return;
    // 内蔵の発振器は精度が低いので、ときどき合わせ直す。
    if (net::syncDue()) sync();
    render(++minutes_ >= kCleanEveryMinutes ? Update::Text : Update::Fast);
    if (minutes_ >= kCleanEveryMinutes) minutes_ = 0;
    armNextMinute();
  }

  // 次の分の変わり目まで眠る。
  uint32_t wakeIntervalSec() override {
    if (setting_ || !net::timeValid()) return 0;
    return secondsToNextMinute();
  }

 private:
  static uint32_t secondsToNextMinute() {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return 60 - t.tm_sec;
  }

  void armNextMinute() {
    if (net::timeValid()) os.setTick(secondsToNextMinute() * 1000UL);
  }

  void sync() {
    ui::message(TR("時刻を合わせています…", "Setting the clock..."));
    syncError_ = net::connectAndSync() ? "" : net::lastError();
    os.touch();
  }

  void beginSetting() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    hour_ = net::timeValid() ? t.tm_hour : 12;
    minute_ = net::timeValid() ? t.tm_min : 0;
    field_ = 0;
    setting_ = true;
    drawSetting();
  }

  void settingKey(Key k) {
    int& value = field_ == 0 ? hour_ : minute_;
    const int range = field_ == 0 ? 24 : 60;
    switch (k) {
      case Key::A:
        value = (value + range - 1) % range;
        drawSetting();
        break;
      case Key::B:
        value = (value + 1) % range;
        drawSetting();
        break;
      case Key::LongB:
        if (field_ == 0) {
          field_ = 1;
          drawSetting();
        } else {
          apply();
        }
        break;
      case Key::LongA:
        setting_ = false;
        render(Update::Text);
        armNextMinute();
        break;
      default:
        break;
    }
  }

  void apply() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    // 一度も合っていなければ、日付は仮に 2026-01-01 から始める。
    if (!net::timeValid()) {
      t.tm_year = 2026 - 1900;
      t.tm_mon = 0;
      t.tm_mday = 1;
    }
    t.tm_hour = hour_;
    t.tm_min = minute_;
    t.tm_sec = 0;
    struct timeval tv = {mktime(&t), 0};
    settimeofday(&tv, nullptr);
    setting_ = false;
    render(Update::Text);
    armNextMinute();
  }

  void drawSetting() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("時刻を合わせる", "Set the clock"));
    g.setPen(0);
    char line[16];
    snprintf(line, sizeof(line), "%02d:%02d", hour_, minute_);
    constexpr int16_t kEm = 150;
    g.textCentered(line, Gfx::W / 2, Gfx::H / 2, kEm);
    g.textCentered(field_ == 0 ? TR("時を選んでいます", "Choosing the hour") : TR("分を選んでいます", "Choosing the minute"), Gfx::W / 2, Gfx::H / 2 + 80, 28);
    ui::footer(TR("A:-1 B:+1 B長押し:次へ A長押し:戻る", "A:-1 B:+1 Hold B:Next Hold A:Back"));
    g.update(Update::Fast);
  }

  void render(Update mode) {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("時計", "Clock"));
    g.setPen(0);
    if (net::timeValid()) {
      const time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      char line[32];
      snprintf(line, sizeof(line), "%02d:%02d", t.tm_hour, t.tm_min);
      int16_t em = 190;
      while (em > 80 && g.textWidth(line, em) > Gfx::W - 24) em -= 10;
      g.textCentered(line, Gfx::W / 2, Gfx::H / 2, em);
      static const char* const kWeekJa[] = {"日", "月", "火", "水", "木", "金", "土"};
      static const char* const kWeekEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      if (i18n::lang() == i18n::Lang::Ja) {
        snprintf(line, sizeof(line), "%d年%d月%d日（%s）", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, kWeekJa[t.tm_wday]);
      } else {
        snprintf(line, sizeof(line), "%s %d-%02d-%02d", kWeekEn[t.tm_wday], t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
      }
      g.textCentered(line, Gfx::W / 2, Gfx::H / 2 + 80, 34);
    } else {
      g.textCentered("--:--", Gfx::W / 2, Gfx::H / 2, 190);
      g.textCentered(TR("時刻が合っていません", "The clock is not set"), Gfx::W / 2, Gfx::H / 2 + 80, 28);
      g.textCentered(TR("B 長押しで NTP、A で手動", "Hold B for NTP, A to set by hand"), Gfx::W / 2, Gfx::H / 2 + 124, 22);
    }
    if (syncError_[0] != '\0') g.textCentered(syncError_, Gfx::W / 2, Gfx::H / 2 + 180, 22);
    ui::footer(TR("A:手動 B:更新 B長押し:NTP A長押し:戻る", "A:Manual B:Refresh Hold B:NTP Hold A:Back"));
    g.update(mode);
  }

  bool setting_ = false;
  int hour_ = 0;
  int minute_ = 0;
  int field_ = 0;
  int minutes_ = 0;
  const char* syncError_ = "";
};

ClockApp gApp;

}  // namespace

App& clockApp() { return gApp; }

}  // namespace pdos
