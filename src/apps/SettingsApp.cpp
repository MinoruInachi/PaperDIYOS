// 設定。言語、Wi-Fi、NTP、眠るまでの時間、電源。
//
// どの一覧も A:上 B:下 B長押し:決定 A長押し:戻る。文字を打つところだけ
// os/Keyboard.h の入力画面に切り替わる。

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Apps.h"
#include "os/I18n.h"
#include "os/Icons.h"
#include "os/Keyboard.h"
#include "os/Net.h"
#include "os/Ui.h"

namespace pdos {
namespace {

enum class Screen : uint8_t { Main, Wifi, WifiScan, Ntp, NtpServer, Timezone, Keyboard };

enum MainItem : uint16_t { MainLanguage, MainWifi, MainNtp, MainIdle, MainClean, MainPowerOff, MainCount };
enum WifiItem : uint16_t { WifiCurrent, WifiChoose, WifiTest, WifiForget, WifiCount };
enum NtpItem : uint16_t { NtpNow, NtpSyncNow, NtpServerItem, NtpZone, NtpAuto, NtpCount };

constexpr uint16_t kIdleChoices[] = {30, 60, 120, 300, 600, 0};

struct Zone {
  const char* ja;
  const char* en;
  const char* posix;
};
constexpr Zone kZones[] = {
    {"日本（JST）", "Japan (JST)", "JST-9"},
    {"韓国（KST）", "Korea (KST)", "KST-9"},
    {"中国（CST）", "China (CST)", "CST-8"},
    {"インド（IST）", "India (IST)", "IST-5:30"},
    {"協定世界時（UTC）", "UTC", "UTC0"},
    {"ロンドン", "London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"パリ・ベルリン", "Paris / Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"ニューヨーク", "New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"シカゴ", "Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"ロサンゼルス", "Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"シドニー", "Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};
constexpr uint16_t kZoneCount = sizeof(kZones) / sizeof(kZones[0]);

constexpr const char* kNtpPresets[] = {"ntp.nict.jp", "ntp.jst.mfeed.ad.jp", "time.google.com", "pool.ntp.org"};
constexpr uint16_t kNtpPresetCount = sizeof(kNtpPresets) / sizeof(kNtpPresets[0]);

const char* zoneName() {
  const char* tz = net::config().timezone;
  for (const Zone& z : kZones) {
    if (strcmp(z.posix, tz) == 0) return TR(z.ja, z.en);
  }
  return tz;
}

class SettingsApp : public App {
 public:
  const char* id() const override { return "settings"; }
  const char* name() const override { return TR("設定", "Settings"); }
  const uint8_t* icon() const override { return icons::settings; }

  void start(bool, Key) override { enter(Screen::Main, Update::Text); }

  void key(Key k) override {
    if (screen_ == Screen::Keyboard) {
      keyboardKey(k);
      return;
    }
    switch (k) {
      case Key::A:
        list_.up();
        render(Update::Fast);
        break;
      case Key::B:
        list_.down();
        render(Update::Fast);
        break;
      case Key::LongB:
        activate();
        break;
      case Key::LongA:
        back();
        break;
      default:
        break;
    }
  }

 private:
  void enter(Screen s, Update mode = Update::Fast) {
    screen_ = s;
    message_ = "";
    switch (s) {
      case Screen::Main: list_.reset(MainCount); break;
      case Screen::Wifi: list_.reset(WifiCount, WifiChoose); break;
      case Screen::WifiScan: list_.reset(static_cast<uint16_t>(scanCount_ + 1)); break;
      case Screen::Ntp: list_.reset(NtpCount, NtpSyncNow); break;
      case Screen::NtpServer: list_.reset(kNtpPresetCount + 1); break;
      case Screen::Timezone: list_.reset(kZoneCount); break;
      case Screen::Keyboard: break;
    }
    render(mode);
  }

  void back() {
    switch (screen_) {
      case Screen::Main: os.goHome(); break;
      case Screen::WifiScan: enter(Screen::Wifi); break;
      case Screen::NtpServer:
      case Screen::Timezone: enter(Screen::Ntp); break;
      default: enter(Screen::Main); break;
    }
  }

  // --- 一覧の中身 -------------------------------------------------------------

  static void label(uint16_t i, char* buf, size_t cap, void* self) {
    auto* app = static_cast<SettingsApp*>(self);
    switch (app->screen_) {
      case Screen::Main: app->mainLabel(i, buf, cap); break;
      case Screen::Wifi: app->wifiLabel(i, buf, cap); break;
      case Screen::WifiScan: app->scanLabel(i, buf, cap); break;
      case Screen::Ntp: app->ntpLabel(i, buf, cap); break;
      case Screen::NtpServer: app->serverLabel(i, buf, cap); break;
      case Screen::Timezone: snprintf(buf, cap, "%s", TR(kZones[i].ja, kZones[i].en)); break;
      default: break;
    }
  }

  void mainLabel(uint16_t i, char* buf, size_t cap) {
    switch (i) {
      case MainLanguage:
        snprintf(buf, cap, "%s", TR("言語: 日本語", "Language: English"));
        break;
      case MainWifi: {
        const auto& c = net::config();
        snprintf(buf, cap, TR("Wi-Fi: %s", "Wi-Fi: %s"), c.ssid[0] != '\0' ? c.ssid : TR("未設定", "not set"));
        break;
      }
      case MainNtp:
        snprintf(buf, cap, "%s", TR("時刻（NTP）", "Clock (NTP)"));
        break;
      case MainIdle: {
        const uint16_t s = os.idleSleepSec();
        if (s == 0) {
          snprintf(buf, cap, "%s", TR("眠るまで: 眠らない", "Sleep after: never"));
        } else if (s < 60) {
          snprintf(buf, cap, TR("眠るまで: %u 秒", "Sleep after: %u s"), s);
        } else {
          snprintf(buf, cap, TR("眠るまで: %u 分", "Sleep after: %u min"), s / 60);
        }
        break;
      }
      case MainClean:
        snprintf(buf, cap, "%s", TR("画面の残像を消す", "Clean the screen"));
        break;
      default:
        snprintf(buf, cap, "%s", TR("電源を切る", "Power off"));
        break;
    }
  }

  void wifiLabel(uint16_t i, char* buf, size_t cap) {
    const auto& c = net::config();
    switch (i) {
      case WifiCurrent:
        snprintf(buf, cap, TR("接続先: %s", "Network: %s"), c.ssid[0] != '\0' ? c.ssid : TR("未設定", "not set"));
        break;
      case WifiChoose:
        snprintf(buf, cap, "%s", TR("ネットワークを選ぶ", "Choose a network"));
        break;
      case WifiTest:
        snprintf(buf, cap, "%s", TR("接続を試す", "Test the connection"));
        break;
      default:
        snprintf(buf, cap, "%s", TR("接続先を消す", "Forget this network"));
        break;
    }
  }

  void scanLabel(uint16_t i, char* buf, size_t cap) {
    if (static_cast<int>(i) < scanCount_) {
      snprintf(buf, cap, "%s %s (%d dBm)", net::scanSecured(i) ? "■" : "□", net::scanSsid(i), net::scanRssi(i));
    } else {
      snprintf(buf, cap, "%s", TR("名前を入力する（隠れた SSID）", "Enter a name (hidden SSID)"));
    }
  }

  void ntpLabel(uint16_t i, char* buf, size_t cap) {
    const auto& c = net::config();
    switch (i) {
      case NtpNow: {
        if (!net::timeValid()) {
          snprintf(buf, cap, "%s", TR("いま: 未設定", "Now: not set"));
        } else {
          const time_t now = time(nullptr);
          struct tm t;
          localtime_r(&now, &t);
          snprintf(buf, cap, TR("いま: %d-%02d-%02d %02d:%02d", "Now: %d-%02d-%02d %02d:%02d"), t.tm_year + 1900,
                   t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
        }
        break;
      }
      case NtpSyncNow:
        snprintf(buf, cap, "%s", TR("いま合わせる", "Sync now"));
        break;
      case NtpServerItem:
        snprintf(buf, cap, TR("サーバー: %s", "Server: %s"), c.ntpServer);
        break;
      case NtpZone:
        snprintf(buf, cap, TR("地域: %s", "Time zone: %s"), zoneName());
        break;
      default:
        snprintf(buf, cap, "%s", c.autoSync ? TR("自動で合わせる: する", "Auto sync: on") : TR("自動で合わせる: しない", "Auto sync: off"));
        break;
    }
  }

  void serverLabel(uint16_t i, char* buf, size_t cap) {
    if (i < kNtpPresetCount) {
      snprintf(buf, cap, "%s %s", strcmp(net::config().ntpServer, kNtpPresets[i]) == 0 ? "■" : "□", kNtpPresets[i]);
    } else {
      snprintf(buf, cap, "%s", TR("名前を入力する", "Enter a name"));
    }
  }

  // --- 決定 -------------------------------------------------------------------

  void activate() {
    switch (screen_) {
      case Screen::Main: activateMain(); break;
      case Screen::Wifi: activateWifi(); break;
      case Screen::WifiScan: activateScan(); break;
      case Screen::Ntp: activateNtp(); break;
      case Screen::NtpServer: activateServer(); break;
      case Screen::Timezone: activateZone(); break;
      default: break;
    }
  }

  void activateMain() {
    Gfx& g = os.gfx();
    switch (list_.selected()) {
      case MainLanguage:
        i18n::set(i18n::lang() == i18n::Lang::Ja ? i18n::Lang::En : i18n::Lang::Ja);
        render(Update::Text);
        break;
      case MainWifi:
        enter(Screen::Wifi);
        break;
      case MainNtp:
        enter(Screen::Ntp);
        break;
      case MainIdle: {
        size_t next = 0;
        const size_t n = sizeof(kIdleChoices) / sizeof(kIdleChoices[0]);
        for (size_t i = 0; i < n; ++i) {
          if (kIdleChoices[i] == os.idleSleepSec()) next = (i + 1) % n;
        }
        os.setIdleSleepSec(kIdleChoices[next]);
        render(Update::Fast);
        break;
      }
      case MainClean:
        g.clear(0);
        g.update(Update::Quality);
        g.clear();
        g.update(Update::Quality);
        render(Update::Text);
        break;
      case MainPowerOff:
        ui::message(TR("電源を切ります", "Powering off"), TR("電源ボタンで入ります", "Press the power button to start"));
        os.powerOff();
        break;
      default:
        break;
    }
  }

  void activateWifi() {
    switch (list_.selected()) {
      case WifiChoose:
        ui::message(TR("電波を探しています…", "Scanning..."));
        scanCount_ = net::scan();
        os.touch();
        enter(Screen::WifiScan, Update::Text);
        break;
      case WifiTest:
        testConnection();
        break;
      case WifiForget:
        net::forgetWifi();
        message_ = TR("接続先を消しました", "Network forgotten");
        render(Update::Fast);
        break;
      default:
        break;
    }
  }

  void testConnection() {
    if (!net::configured()) {
      message_ = TR("接続先がありません", "No network set");
      render(Update::Fast);
      return;
    }
    ui::message(TR("つないでいます…", "Connecting..."), net::ssid());
    const bool ok = net::connect();
    snprintf(result_, sizeof(result_), ok ? TR("つながりました: %s", "Connected: %s") : "%s", ok ? net::address() : net::lastError());
    net::stop();
    os.touch();
    message_ = result_;
    render(Update::Text);
  }

  void activateScan() {
    const int index = list_.selected();
    keyboardPurpose_ = KeyboardPurpose::Password;
    if (index < scanCount_) {
      snprintf(pendingSsid_, sizeof(pendingSsid_), "%s", net::scanSsid(index));
      if (!net::scanSecured(index)) {
        saveNetwork("");
        return;
      }
      openPassword();
    } else {
      keyboardPurpose_ = KeyboardPurpose::Ssid;
      keyboard_.open(TR("SSID を入力", "Enter the SSID"), "", 32, false);
      screen_ = Screen::Keyboard;
      keyboard_.draw();
    }
  }

  void openPassword() {
    keyboardPurpose_ = KeyboardPurpose::Password;
    snprintf(keyboardTitle_, sizeof(keyboardTitle_), TR("%s のパスワード", "Password for %s"), pendingSsid_);
    keyboard_.open(keyboardTitle_, "", 64, true);
    screen_ = Screen::Keyboard;
    keyboard_.draw();
  }

  void saveNetwork(const char* psk) {
    auto& c = net::config();
    snprintf(c.ssid, sizeof(c.ssid), "%s", pendingSsid_);
    snprintf(c.psk, sizeof(c.psk), "%s", psk);
    net::saveConfig();
    screen_ = Screen::Wifi;
    list_.reset(WifiCount, WifiTest);
    testConnection();
  }

  void activateNtp() {
    auto& c = net::config();
    switch (list_.selected()) {
      case NtpNow:
        render(Update::Fast);
        break;
      case NtpSyncNow: {
        ui::message(TR("時刻を合わせています…", "Setting the clock..."), c.ntpServer);
        const bool ok = net::connectAndSync();
        os.touch();
        message_ = ok ? TR("合わせました", "Clock set") : net::lastError();
        render(Update::Text);
        break;
      }
      case NtpServerItem:
        enter(Screen::NtpServer);
        break;
      case NtpZone:
        enter(Screen::Timezone);
        break;
      default:
        c.autoSync = !c.autoSync;
        net::saveConfig();
        render(Update::Fast);
        break;
    }
  }

  void activateServer() {
    auto& c = net::config();
    const int index = list_.selected();
    if (index < kNtpPresetCount) {
      snprintf(c.ntpServer, sizeof(c.ntpServer), "%s", kNtpPresets[index]);
      net::saveConfig();
      enter(Screen::Ntp);
    } else {
      keyboardPurpose_ = KeyboardPurpose::NtpServer;
      keyboard_.open(TR("NTP サーバーを入力", "Enter the NTP server"), c.ntpServer, sizeof(c.ntpServer) - 1, false);
      screen_ = Screen::Keyboard;
      keyboard_.draw();
    }
  }

  void activateZone() {
    auto& c = net::config();
    snprintf(c.timezone, sizeof(c.timezone), "%s", kZones[list_.selected()].posix);
    net::saveConfig();
    net::applyTimezone();
    enter(Screen::Ntp);
  }

  // --- 文字入力 ---------------------------------------------------------------

  void keyboardKey(Key k) {
    const ui::Keyboard::Result r = keyboard_.key(k);
    if (r == ui::Keyboard::Result::Editing) return;
    const bool done = r == ui::Keyboard::Result::Done;
    switch (keyboardPurpose_) {
      case KeyboardPurpose::Ssid:
        if (done && keyboard_.text()[0] != '\0') {
          snprintf(pendingSsid_, sizeof(pendingSsid_), "%s", keyboard_.text());
          openPassword();
        } else {
          enter(Screen::WifiScan);
        }
        break;
      case KeyboardPurpose::Password:
        if (done) {
          saveNetwork(keyboard_.text());
        } else {
          enter(Screen::WifiScan);
        }
        break;
      case KeyboardPurpose::NtpServer:
        if (done && keyboard_.text()[0] != '\0') {
          auto& c = net::config();
          snprintf(c.ntpServer, sizeof(c.ntpServer), "%s", keyboard_.text());
          net::saveConfig();
        }
        enter(Screen::Ntp);
        break;
    }
  }

  // --- 描画 -------------------------------------------------------------------

  const char* title() const {
    switch (screen_) {
      case Screen::Wifi: return "Wi-Fi";
      case Screen::WifiScan: return TR("ネットワークを選ぶ", "Choose a network");
      case Screen::Ntp: return TR("時刻（NTP）", "Clock (NTP)");
      case Screen::NtpServer: return TR("NTP サーバー", "NTP server");
      case Screen::Timezone: return TR("地域", "Time zone");
      default: return TR("設定", "Settings");
    }
  }

  void render(Update mode) {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(title());
    list_.draw(label, this);
    if (message_[0] != '\0') {
      g.setPen(0);
      g.textWrap(message_, ui::kMargin, Gfx::H - ui::kFooterH - 60, Gfx::W - ui::kMargin * 2, 24, 32, 2);
    }
    ui::footer(TR("A:上 B:下 B長押し:決定 A長押し:戻る", "A:Up B:Down Hold B:OK Hold A:Back"));
    g.update(mode);
  }

  enum class KeyboardPurpose : uint8_t { Ssid, Password, NtpServer };

  Screen screen_ = Screen::Main;
  ui::ListView list_;
  ui::Keyboard keyboard_;
  KeyboardPurpose keyboardPurpose_ = KeyboardPurpose::Password;
  int scanCount_ = 0;
  char pendingSsid_[33] = {0};
  char keyboardTitle_[64] = {0};
  char result_[64] = {0};
  const char* message_ = "";
};

SettingsApp gApp;

}  // namespace

App& settingsApp() { return gApp; }

}  // namespace pdos
