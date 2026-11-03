#include "I18n.h"
#include "Net.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <string.h>
#include <time.h>

#include "os/TextFile.h"
#include "os/Store.h"

namespace pdos {
namespace net {
namespace {

constexpr const char* kWifiPath = "/config/wifi.txt";
constexpr const char* kStoreKey = "net.cfg";
constexpr const char* kFallbackNtp = "pool.ntp.org";
constexpr long kResyncSec = 6 * 3600;
constexpr int kMaxScan = 32;
constexpr time_t kTimeValidAfter = 1704067200;  // 2024-01-01

Config gConfig;
bool gConfigLoaded = false;
char gSsid[33] = "";
char gPsk[65] = "";
char gAddress[40] = "";
const char* gError = "";
bool gHotspot = false;
struct ScanEntry {
  char ssid[33];
  int8_t rssi;
  bool secured;
};
ScanEntry* gScan = nullptr;
int gScanCount = 0;

void ensureLoaded() {
  if (gConfigLoaded) return;
  gConfigLoaded = true;
  store::load(kStoreKey, gConfig);
}

// 接続先を決める。設定アプリで選んだものが先、無ければ SD のファイル。
bool loadCredentials() {
  ensureLoaded();
  gSsid[0] = gPsk[0] = '\0';
  if (gConfig.ssid[0] != '\0') {
    snprintf(gSsid, sizeof(gSsid), "%s", gConfig.ssid);
    snprintf(gPsk, sizeof(gPsk), "%s", gConfig.psk);
    return true;
  }
  if (!SDCardManager::getInstance().ready()) return false;
  textfile::Reader r;
  if (!r.open(kWifiPath)) return false;
  char line[128];
  while (r.readLine(line, sizeof(line))) {
    char* eq = strchr(line, '=');
    if (eq == nullptr || line[0] == '#') continue;
    *eq = '\0';
    textfile::trim(line);
    char* value = eq + 1;
    textfile::trim(value);
    if (strcmp(line, "ssid") == 0) snprintf(gSsid, sizeof(gSsid), "%s", value);
    if (strcmp(line, "psk") == 0) snprintf(gPsk, sizeof(gPsk), "%s", value);
  }
  return gSsid[0] != '\0';
}

}  // namespace

Config& config() {
  ensureLoaded();
  return gConfig;
}

void saveConfig() {
  ensureLoaded();
  store::save(kStoreKey, gConfig);
}

void forgetWifi() {
  ensureLoaded();
  gConfig.ssid[0] = gConfig.psk[0] = '\0';
  saveConfig();
}

bool configured() { return loadCredentials(); }

bool connect(uint32_t timeoutMs) {
  gHotspot = false;
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!loadCredentials()) {
    gError = TR("/config/wifi.txt がありません", "Wi-Fi is not set up");
    return false;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(gSsid, gPsk);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    gError = TR("Wi-Fi につながりません", "Could not connect");
    stop();
    return false;
  }
  snprintf(gAddress, sizeof(gAddress), "%s", WiFi.localIP().toString().c_str());
  gError = "";
  return true;
}

bool startHotspot() {
  gHotspot = true;
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  snprintf(gSsid, sizeof(gSsid), "PaperDIY");
  if (!WiFi.softAP(gSsid)) {
    gError = TR("親機になれません", "Could not start hotspot");
    return false;
  }
  snprintf(gAddress, sizeof(gAddress), "%s", WiFi.softAPIP().toString().c_str());
  return true;
}

void stop() {
  gHotspot = false;
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

bool online() { return gHotspot || WiFi.status() == WL_CONNECTED; }
const char* ssid() { return gSsid; }
const char* address() { return gAddress; }
int rssi() { return WiFi.RSSI(); }
const char* lastError() { return gError; }

void applyTimezone() {
  ensureLoaded();
  setenv("TZ", gConfig.timezone, 1);
  tzset();
}

bool syncTime(uint32_t timeoutMs) {
  ensureLoaded();
  // 無線を一度も上げていないと lwIP が動いておらず、SNTP を触るとアサートで落ちる。
  if (WiFi.status() != WL_CONNECTED) {
    gError = TR("Wi-Fi につながっていません", "Wi-Fi is not connected");
    return false;
  }
  // 前回の同期の結果が残らないように、止めてから始める。
  esp_sntp_stop();
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(gConfig.timezone, gConfig.ntpServer, kFallbackNtp);
  const uint32_t start = millis();
  // sntp_get_sync_status は完了を一度返すと RESET に戻るので、読むのは 1 回きり。
  bool synced = false;
  while (!synced && millis() - start < timeoutMs) {
    synced = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
    if (!synced) delay(100);
  }
  const bool ok = synced && time(nullptr) > kTimeValidAfter;
  esp_sntp_stop();
  if (!ok) {
    gError = TR("時刻を取得できません", "Could not get the time");
    return false;
  }
  gConfig.lastSync = time(nullptr);
  saveConfig();
  return true;
}

bool connectAndSync() {
  if (!connect()) return false;
  const bool ok = syncTime();
  stop();
  return ok;
}

bool syncDue() {
  ensureLoaded();
  if (!gConfig.autoSync || !configured()) return false;
  if (!timeValid()) return true;
  return time(nullptr) - static_cast<time_t>(gConfig.lastSync) >= kResyncSec;
}

int scan() {
  scanRelease();
  if (gScan == nullptr) {
    gScan = static_cast<ScanEntry*>(heap_caps_malloc(sizeof(ScanEntry) * kMaxScan, MALLOC_CAP_SPIRAM));
    if (gScan == nullptr) return 0;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  const int found = WiFi.scanNetworks();
  for (int i = 0; i < found; ++i) {
    const String name = WiFi.SSID(i);
    if (name.length() == 0) continue;
    // 同じ SSID は一番強いものだけ残す。
    int dup = -1;
    for (int j = 0; j < gScanCount; ++j) {
      if (strcmp(gScan[j].ssid, name.c_str()) == 0) dup = j;
    }
    if (dup >= 0) {
      if (WiFi.RSSI(i) > gScan[dup].rssi) gScan[dup].rssi = static_cast<int8_t>(WiFi.RSSI(i));
      continue;
    }
    if (gScanCount >= kMaxScan) break;
    ScanEntry& e = gScan[gScanCount++];
    snprintf(e.ssid, sizeof(e.ssid), "%s", name.c_str());
    e.rssi = static_cast<int8_t>(WiFi.RSSI(i));
    e.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  stop();
  // 強い順に。
  for (int i = 1; i < gScanCount; ++i) {
    for (int j = i; j > 0 && gScan[j].rssi > gScan[j - 1].rssi; --j) {
      const ScanEntry tmp = gScan[j];
      gScan[j] = gScan[j - 1];
      gScan[j - 1] = tmp;
    }
  }
  return gScanCount;
}

const char* scanSsid(int index) { return gScan[index].ssid; }
int scanRssi(int index) { return gScan[index].rssi; }
bool scanSecured(int index) { return gScan[index].secured; }
void scanRelease() { gScanCount = 0; }

bool timeValid() { return time(nullptr) > kTimeValidAfter; }

}  // namespace net
}  // namespace pdos
