#pragma once

// Wi-Fi のつなぎ方と時刻合わせ。
//
// 接続先は設定アプリで選ぶ（本体の NVS に残す。SD を抜いても読まれない）。
// NVS に無ければ SD の /config/wifi.txt も見る（Badger の WIFI_CONFIG.py に当たる）。
//
//   ssid = 家のWi-Fi
//   psk  = パスワード
//
// 無線は使うあいだだけ上げる。DRAM をかなり使うし、電池も食うので、
// 使い終わったアプリは必ず stop() する。

#include <stdbool.h>
#include <stdint.h>

namespace pdos {
namespace net {

// 時刻合わせの設定。
struct Config {
  char ssid[33] = {0};
  char psk[65] = {0};
  char ntpServer[64] = "ntp.nict.jp";
  char timezone[48] = "JST-9";
  bool autoSync = true;       // 時計を開いたとき、古ければ合わせ直す
  long long lastSync = 0;     // 最後に NTP で合わせた時刻（Unix 秒）
};

// いまの設定。loadConfig のあとは変更して saveConfig できる。
Config& config();
void saveConfig();
void forgetWifi();

// 接続先があるか（NVS か /config/wifi.txt）。
bool configured();
// 接続する。だめなら false（lastError に理由）。
bool connect(uint32_t timeoutMs = 15000);
// この機体を親機にする（ファイル転送用）。
bool startHotspot();
void stop();
bool online();

const char* ssid();
const char* address();
int rssi();
const char* lastError();

// 周りの電波を探す。見つかった数を返し、強い順に並べて控える。
int scan();
const char* scanSsid(int index);
int scanRssi(int index);
bool scanSecured(int index);
void scanRelease();

// NTP で時刻を合わせる。接続済みで呼ぶ。
bool syncTime(uint32_t timeoutMs = 10000);
// つないで合わせて切る。設定アプリと時計から使う。
bool connectAndSync();
// 自動同期がオンで、合わせてから kResyncSec 以上たっていれば true。
bool syncDue();
// 時刻が一度でも合っているか。
bool timeValid();
// タイムゾーンを当てる（設定アプリで選んだもの。POSIX 形式）。
void applyTimezone();

}  // namespace net
}  // namespace pdos
