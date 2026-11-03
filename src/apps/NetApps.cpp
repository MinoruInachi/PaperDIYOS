// Wi-Fi を使うアプリ。Badger 2040 W の net_info.py / news.py / weather.py に、
// ファイルの転送を足したもの。無線は使うあいだだけ上げて、すぐ落とす。

#include <Arduino.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "os/I18n.h"
#include "Apps.h"
#include "Parse.h"
#include "os/Icons.h"
#include "os/Net.h"
#include "os/Store.h"
#include "os/TextFile.h"
#include "os/Ui.h"
#include "reader/FileServer.h"
#include "reader/Http.h"

namespace pdos {
namespace {

// 取ってきたものを PSRAM の箱へためる。あふれたら打ち切る（先頭だけで足りる）。
struct Buffer {
  char* data = nullptr;
  size_t len = 0;
  size_t cap = 0;

  bool alloc(size_t size) {
    cap = size;
    len = 0;
    data = static_cast<char*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
    return data != nullptr;
  }
  void release() {
    heap_caps_free(data);
    data = nullptr;
  }
};

bool bufferSink(void* user, const char* chunk, size_t size) {
  auto* b = static_cast<Buffer*>(user);
  const size_t room = b->cap - 1 - b->len;
  const size_t n = size < room ? size : room;
  memcpy(b->data + b->len, chunk, n);
  b->len += n;
  b->data[b->len] = '\0';
  return n == size;  // いっぱいになったら打ち切る
}

// --- 無線の情報 ---------------------------------------------------------------

class NetInfoApp : public App {
 public:
  const char* id() const override { return "netinfo"; }
  const char* name() const override { return TR("無線", "Network"); }
  const uint8_t* icon() const override { return icons::net; }

  void start(bool, Key) override { refresh(); }

  void key(Key k) override {
    if (k == Key::LongB || k == Key::A || k == Key::B) refresh();
    if (k == Key::LongA) os.goHome();
  }

 private:
  void refresh() {
    Gfx& g = os.gfx();
    if (!os.sdReady() || !net::configured()) {
      ui::message(TR("Wi-Fi の設定がありません", "Wi-Fi is not set up"), TR("/config/wifi.txt を書いてください", "See Settings > Wi-Fi"));
      return;
    }
    ui::message(TR("つないでいます…", "Connecting..."), net::ssid());
    const bool ok = net::connect();
    os.touch();
    g.clear();
    ui::header(TR("無線", "Network"));
    int16_t y = ui::kHeaderH + 60;
    auto row = [&](const char* label, const char* value) {
      g.setPen(0);
      g.text(label, ui::kMargin, y, 24);
      g.textRight(value, Gfx::W - ui::kMargin, y, 24);
      g.hline(ui::kMargin, y + 14, Gfx::W - ui::kMargin * 2, 1);
      y += 56;
    };
    row("SSID", net::ssid());
    row(TR("状態", "Status"), ok ? TR("つながった", "connected") : net::lastError());
    char buf[32];
    snprintf(buf, sizeof(buf), "%s", WiFi.macAddress().c_str());
    row("MAC", buf);
    if (ok) {
      row("IP", net::address());
      snprintf(buf, sizeof(buf), "%d dBm", net::rssi());
      row(TR("電波", "Signal"), buf);
      snprintf(buf, sizeof(buf), "%s", WiFi.gatewayIP().toString().c_str());
      row(TR("ゲートウェイ", "Gateway"), buf);
    }
    net::stop();
    ui::footer(TR("A/B:やり直す A長押し:戻る", "A/B:Retry Hold A:Back"));
    g.update(Update::Text);
  }
};

// --- 天気 ---------------------------------------------------------------------

class WeatherApp : public App {
 public:
  const char* id() const override { return "weather"; }
  const char* name() const override { return TR("天気", "Weather"); }
  const uint8_t* icon() const override { return icons::weather; }

  void start(bool resumed, Key) override {
    loadLocation();
    // 眠りから戻っただけなら、覚えている結果を出す。無線は上げない。
    if (resumed && store::load("weather", saved_) && saved_.w.valid) {
      render();
      return;
    }
    fetch();
  }

  void key(Key k) override {
    if (k == Key::LongB || k == Key::A || k == Key::B) fetch();
    if (k == Key::LongA) os.goHome();
  }

 private:
  struct Saved {
    parse::Weather w;
    time_t at = 0;
  };

  void loadLocation() {
    snprintf(lat_, sizeof(lat_), "35.68");
    snprintf(lon_, sizeof(lon_), "139.76");
    snprintf(place_, sizeof(place_), TR("東京", "Tokyo"));
    if (!os.sdReady()) return;
    textfile::Reader r;
    if (!r.open("/config/location.txt")) return;
    char line[96];
    while (r.readLine(line, sizeof(line))) {
      textfile::trim(line);
      if (line[0] == '\0' || line[0] == '#') continue;
      char* a = strchr(line, ',');
      char* b = a != nullptr ? strchr(a + 1, ',') : nullptr;
      if (b == nullptr) continue;
      *a = *b = '\0';
      textfile::trim(line);
      textfile::trim(a + 1);
      textfile::trim(b + 1);
      snprintf(lat_, sizeof(lat_), "%s", line);
      snprintf(lon_, sizeof(lon_), "%s", a + 1);
      snprintf(place_, sizeof(place_), "%s", b + 1);
      break;
    }
  }

  void fetch() {
    if (!os.sdReady() || !net::configured()) {
      ui::message(TR("Wi-Fi の設定がありません", "Wi-Fi is not set up"), TR("/config/wifi.txt を書いてください", "See Settings > Wi-Fi"));
      return;
    }
    ui::message(TR("天気を取りに行きます…", "Fetching the weather..."), place_);
    if (!net::connect()) {
      ui::message(net::lastError());
      return;
    }
    if (!net::timeValid()) net::syncTime();
    char url[320];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code"
             "&daily=temperature_2m_max,temperature_2m_min,weather_code"
             "&timezone=Asia%%2FTokyo&forecast_days=3&wind_speed_unit=ms",
             lat_, lon_);
    Buffer buf;
    bool ok = false;
    if (buf.alloc(16 * 1024)) {
      const paperdiy::HttpResult res = paperdiy::httpGet(url, bufferSink, &buf);
      if (res.ok) ok = parse::parseWeather(buf.data, &saved_.w);
      buf.release();
    }
    net::stop();
    os.touch();
    if (!ok) {
      ui::message(TR("天気を読み取れませんでした", "Could not read the weather"));
      return;
    }
    saved_.at = time(nullptr);
    store::save("weather", saved_);
    render();
  }

  static const uint8_t* skyIcon(int code) {
    switch (parse::weatherSky(code)) {
      case parse::Sky::Sun: return icons::sun;
      case parse::Sky::Cloud: return icons::cloud;
      case parse::Sky::Fog: return icons::fog;
      case parse::Sky::Snow: return icons::snow;
      case parse::Sky::Storm: return icons::storm;
      default: return icons::rain;
    }
  }

  void render() {
    Gfx& g = os.gfx();
    const auto& w = saved_.w;
    g.clear();
    ui::header(place_);
    g.setPen(0);
    g.bitmap(skyIcon(w.code), icons::kSize, icons::kSize, (Gfx::W - icons::kSize * 2) / 2, ui::kHeaderH + 24, 2);
    g.textCentered(parse::weatherText(w.code, i18n::lang() == i18n::Lang::En), Gfx::W / 2, 320, 36);
    char line[48];
    snprintf(line, sizeof(line), "%.1f°C", w.temperature);
    g.textCentered(line, Gfx::W / 2, 430, 88);
    snprintf(line, sizeof(line), TR("湿度 %d%%   風 %.1f m/s", "Humidity %d%%   Wind %.1f m/s"), w.humidity, w.wind);
    g.textCentered(line, Gfx::W / 2, 490, 26);

    const char* const kDays[] = {TR("今日", "Today"), TR("明日", "Tmrw"), TR("明後日", "+2 days")};
    int16_t y = 550;
    for (int i = 0; i < 3; ++i) {
      g.setPen(0);
      g.hline(ui::kMargin, y, Gfx::W - ui::kMargin * 2, 1);
      g.text(kDays[i], ui::kMargin, y + 62, 28);
      g.bitmap(skyIcon(w.dayCode[i]), icons::kSize, icons::kSize, 150, y + 2);
      snprintf(line, sizeof(line), "%.0f / %.0f°C", w.maxTemp[i], w.minTemp[i]);
      g.textRight(line, Gfx::W - ui::kMargin, y + 62, 30);
      y += 100;
    }
    char hint[64];
    snprintf(hint, sizeof(hint), "%s", TR("A/B:更新 A長押し:戻る", "A/B:Refresh Hold A:Back"));
    if (saved_.at > 0 && net::timeValid()) {
      struct tm t;
      localtime_r(&saved_.at, &t);
      snprintf(hint, sizeof(hint), TR("A/B:更新 A長押し:戻る  %02d:%02d 取得", "A/B:Refresh Hold A:Back  at %02d:%02d"), t.tm_hour, t.tm_min);
    }
    ui::footer(hint);
    g.update(Update::Text);
  }

  Saved saved_;
  char lat_[16] = "";
  char lon_[16] = "";
  char place_[48] = "";
};

// --- ニュース -----------------------------------------------------------------

class NewsApp : public App {
 public:
  const char* id() const override { return "news"; }
  const char* name() const override { return TR("ニュース", "News"); }
  const uint8_t* icon() const override { return icons::news; }

  void start(bool, Key) override {
    if (!os.sdReady() || !net::configured()) {
      ui::message(TR("Wi-Fi の設定がありません", "Wi-Fi is not set up"), TR("/config/wifi.txt を書いてください", "See Settings > Wi-Fi"));
      return;
    }
    if (items_ == nullptr) {
      items_ = static_cast<parse::NewsItem*>(heap_caps_malloc(sizeof(parse::NewsItem) * kMaxItems, MALLOC_CAP_SPIRAM));
    }
    loadFeeds();
    store::load("news", feed_);
    if (feed_ >= feedCount_) feed_ = 0;
    fetch();
  }

  void key(Key k) override {
    if (detail_) {
      if (k == Key::LongA) {
        detail_ = false;
        drawList(Update::Text);
      }
      if (k == Key::A || k == Key::B) {
        k == Key::A ? list_.up() : list_.down();
        drawDetail();
      }
      return;
    }
    switch (k) {
      case Key::A:
        list_.up();
        drawList(Update::Fast);
        break;
      case Key::B:
        list_.down();
        drawList(Update::Fast);
        break;
      case Key::LongB:
        // 先頭の行はフィードの切り替え。
        if (list_.selected() == 0) {
          feed_ = feedCount_ > 0 ? static_cast<int16_t>((feed_ + 1) % feedCount_) : 0;
          store::save("news", feed_);
          fetch();
        } else {
          detail_ = true;
          drawDetail();
        }
        break;
      case Key::LongA:
        os.goHome();
        break;
      default:
        break;
    }
  }

 private:
  static constexpr uint16_t kMaxItems = 30;
  static constexpr uint16_t kMaxFeeds = 8;
  struct Feed {
    char name[32];
    char url[160];
  };

  void loadFeeds() {
    feedCount_ = 0;
    textfile::Reader r;
    if (!r.open("/config/news.txt")) return;
    char line[200];
    while (feedCount_ < kMaxFeeds && r.readLine(line, sizeof(line))) {
      textfile::trim(line);
      if (line[0] == '\0' || line[0] == '#') continue;
      char* comma = strchr(line, ',');
      if (comma == nullptr) continue;
      *comma = '\0';
      textfile::trim(line);
      textfile::trim(comma + 1);
      snprintf(feeds_[feedCount_].name, sizeof(Feed::name), "%s", line);
      snprintf(feeds_[feedCount_].url, sizeof(Feed::url), "%s", comma + 1);
      ++feedCount_;
    }
  }

  void fetch() {
    detail_ = false;
    itemCount_ = 0;
    if (feedCount_ == 0) {
      ui::message(TR("フィードがありません", "No feeds"), TR("/config/news.txt を書いてください", "Write /config/news.txt"));
      return;
    }
    ui::message(TR("ニュースを取りに行きます…", "Fetching the news..."), feeds_[feed_].name);
    if (!net::connect()) {
      ui::message(net::lastError());
      return;
    }
    Buffer buf;
    if (buf.alloc(128 * 1024)) {
      paperdiy::httpGet(feeds_[feed_].url, bufferSink, &buf);
      if (buf.len > 0 && items_ != nullptr) itemCount_ = parse::parseRss(buf.data, items_, kMaxItems);
      buf.release();
    }
    net::stop();
    os.touch();
    list_.reset(itemCount_ + 1, itemCount_ > 0 ? 1 : 0);
    drawList(Update::Text);
  }

  static void label(uint16_t i, char* buf, size_t cap, void* self) {
    auto* app = static_cast<NewsApp*>(self);
    if (i == 0) {
      snprintf(buf, cap, TR("▶ %s（切替）", "▶ %s (switch)"), app->feeds_[app->feed_].name);
    } else {
      snprintf(buf, cap, "%s", app->items_[i - 1].title);
    }
  }

  void drawList(Update mode) {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("ニュース", "News"));
    list_.draw(label, this);
    if (itemCount_ == 0) {
      g.setPen(0);
      g.textCentered(TR("記事を読めませんでした", "Could not read the articles"), Gfx::W / 2, Gfx::H / 2, 26);
    }
    ui::footer(TR("A:上 B:下 B長押し:開く A長押し:戻る", "A:Up B:Down Hold B:Open Hold A:Back"));
    g.update(mode);
  }

  void drawDetail() {
    if (list_.selected() == 0) list_.select(1);
    const parse::NewsItem& item = items_[list_.selected() - 1];
    Gfx& g = os.gfx();
    g.clear();
    ui::header(feeds_[feed_].name);
    g.setPen(0);
    int16_t y = g.textWrap(item.title, ui::kMargin, ui::kHeaderH + 50, Gfx::W - ui::kMargin * 2, 32, 46, 5);
    y += 10;
    y = g.textWrap(item.description, ui::kMargin, y, Gfx::W - ui::kMargin * 2, 24, 36, 8);
    // リンクは QR で手元のスマホへ。
    if (item.link[0] != '\0') {
      const int16_t size = 300;
      ui::qr(item.link, (Gfx::W - size) / 2, Gfx::H - ui::kFooterH - size - 8, size);
    }
    ui::footer(TR("A:前 B:次 A長押し:戻る", "A:Prev B:Next Hold A:Back"));
    g.update(Update::Text);
  }

  parse::NewsItem* items_ = nullptr;
  uint16_t itemCount_ = 0;
  Feed feeds_[kMaxFeeds];
  uint16_t feedCount_ = 0;
  int16_t feed_ = 0;
  ui::ListView list_;
  bool detail_ = false;
};

// --- 転送 ---------------------------------------------------------------------

// ブラウザから /books へ本を入れる。Wi-Fi の設定があれば家の Wi-Fi に入り、
// 無ければこの機体が親機（鍵なしの「PaperDIY」）になる。
class TransferApp : public App {
 public:
  const char* id() const override { return "transfer"; }
  const char* name() const override { return TR("転送", "Transfer"); }
  const uint8_t* icon() const override { return icons::transfer; }

  void start(bool, Key) override {
    if (!os.sdReady()) {
      ui::message(TR("SD カードが必要です", "SD card required"));
      return;
    }
    ui::message(TR("無線を上げています…", "Starting Wi-Fi..."));
    hotspot_ = !net::configured() || !net::connect();
    if (hotspot_ && !net::startHotspot()) {
      ui::message(net::lastError());
      return;
    }
    running_ = server_.begin();
    received_ = 0;
    render();
    os.setTick(kLoopMs);
  }

  void key(Key k) override {
    if (k == Key::LongA) os.goHome();
  }

  void tick() override {
    if (!running_) return;
    server_.loop();
    if (server_.received() != received_) {
      received_ = server_.received();
      render();
    }
    os.touch();
    os.setTick(kLoopMs);
  }

  void suspend() override { shutdown(); }
  void stop() override { shutdown(); }
  // 受けているあいだは眠らない。A 長押しで閉じる。
  bool busy() override { return running_; }

 private:
  static constexpr uint32_t kLoopMs = 5;

  void shutdown() {
    if (running_) server_.end();
    running_ = false;
    net::stop();
  }

  void render() {
    Gfx& g = os.gfx();
    char url[64];
    snprintf(url, sizeof(url), "http://%s/", net::address());
    g.clear();
    ui::header(TR("転送", "Transfer"));
    const int16_t size = 400;
    ui::qr(url, (Gfx::W - size) / 2, ui::kHeaderH + 24, size);
    g.setPen(0);
    int16_t y = ui::kHeaderH + size + 80;
    g.textCentered(url, Gfx::W / 2, y, 34);
    y += 60;
    char line[80];
    snprintf(line, sizeof(line), "%s: %s", hotspot_ ? TR("親機（鍵なし）", "Hotspot (open)") : TR("接続先", "Network"), net::ssid());
    g.textCentered(line, Gfx::W / 2, y, 26);
    y += 50;
    g.textWrap(TR("同じネットワークの端末のブラウザで開くと、/books に本を出し入れできます。"
                 "合言葉はありません。使い終わったら戻ってください。",
                 "Open this address in a browser on the same network to manage /books. "
                 "There is no password. Go back when you are done."),
               ui::kMargin, y, Gfx::W - ui::kMargin * 2, 22);
    if (received_ > 0) {
      snprintf(line, sizeof(line), TR("受け取った本: %u 冊", "Books received: %u"), received_);
      g.textCentered(line, Gfx::W / 2, Gfx::H - ui::kFooterH - 30, 28);
    }
    ui::footer(TR("A長押し:戻る", "Hold A:Back"));
    g.update(Update::Text);
  }

  paperdiy::FileServer server_;
  bool running_ = false;
  bool hotspot_ = false;
  uint16_t received_ = 0;
};

NetInfoApp gNetInfo;
WeatherApp gWeather;
NewsApp gNews;
TransferApp gTransfer;

}  // namespace

App& netInfoApp() { return gNetInfo; }
App& weatherApp() { return gWeather; }
App& newsApp() { return gNews; }
App& transferApp() { return gTransfer; }

}  // namespace pdos
