// 本体の情報。Badger の info.py に当たる。

#include <Arduino.h>
#include <BoardPaperDiy.h>
#include <SDCardManager.h>
#include <stdio.h>

#include "os/I18n.h"
#include <pd_bridge.h>

#include "Apps.h"
#include "os/Icons.h"
#include "os/Net.h"
#include "os/Ui.h"
#include "reader/FontStore.h"

namespace pdos {
namespace {

class InfoApp : public App {
 public:
  const char* id() const override { return "info"; }
  const char* name() const override { return TR("情報", "Info"); }
  const uint8_t* icon() const override { return icons::info; }

  void start(bool, Key) override { render(); }

  void key(Key k) override {
    if (k == Key::A || k == Key::B) render();
    if (k == Key::LongA) os.goHome();
  }

 private:
  void row(int16_t& y, const char* label, const char* value) {
    Gfx& g = os.gfx();
    g.setPen(0);
    g.text(label, ui::kMargin, y, 24);
    g.textRight(value, Gfx::W - ui::kMargin, y, 24);
    g.hline(ui::kMargin, y + 14, Gfx::W - ui::kMargin * 2, 1);
    y += 52;
  }

  void render() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("情報", "Info"));
    int16_t y = ui::kHeaderH + 50;
    char buf[64];

    row(y, TR("基板", "Board"), "M5Stack PaperDIY");
    snprintf(buf, sizeof(buf), "%s rev%d @ %luMHz", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
    row(y, "CPU", buf);
    snprintf(buf, sizeof(buf), "%lu MB", ESP.getFlashChipSize() / (1024 * 1024));
    row(y, TR("フラッシュ", "Flash"), buf);
    snprintf(buf, sizeof(buf), TR("空き %lu / %lu KB", "free %lu / %lu KB"), ESP.getFreePsram() / 1024, ESP.getPsramSize() / 1024);
    row(y, "PSRAM", buf);
    snprintf(buf, sizeof(buf), TR("空き %lu KB", "free %lu KB"), ESP.getFreeHeap() / 1024);
    row(y, TR("ヒープ", "Heap"), buf);

    const auto s = os.battery().readStatus();
    if (s.millivoltsKnown) {
      snprintf(buf, sizeof(buf), "%u.%02u V  %u%%", s.millivolts / 1000, (s.millivolts % 1000) / 10, s.percentage);
    } else {
      snprintf(buf, sizeof(buf), TR("不明", "unknown"));
    }
    row(y, TR("電池", "Battery"), buf);
    row(y, TR("外部電源", "USB power"), s.externalPower ? (BoardPaperDiy::isCharging() ? TR("あり（充電中）", "yes (charging)") : TR("あり", "yes")) : TR("なし", "none"));

    if (os.sdReady()) {
      auto& sd = SDCardManager::getInstance();
      snprintf(buf, sizeof(buf), "%llu / %llu MB", static_cast<unsigned long long>(sd.sdUsedBytes() / (1024 * 1024)),
               static_cast<unsigned long long>(sd.sdTotalBytes() / (1024 * 1024)));
    } else {
      snprintf(buf, sizeof(buf), TR("なし", "none"));
    }
    row(y, TR("SD カード", "SD card"), buf);

    auto& fonts = os.fonts();
    snprintf(buf, sizeof(buf), "%s %lu KB", fonts.sourceName(), static_cast<unsigned long>(fonts.totalBytes() / 1024));
    row(y, TR("フォント", "Font"), buf);

    row(y, "MicroPython", pd_py_version());

    snprintf(buf, sizeof(buf), TR("%lu 秒", "%lu s"), millis() / 1000);
    row(y, TR("起動から", "Uptime"), buf);
    row(y, TR("起きた理由", "Wake reason"), os.wakeReason());
    row(y, TR("時刻", "Clock"), net::timeValid() ? TR("合っている", "set") : TR("未設定（時計で同期）", "not set (see Settings)"));
    snprintf(buf, sizeof(buf), TR("描画 %lu ms", "%lu ms"), os.gfx().lastUpdateMs());
    row(y, TR("直前の更新", "Last refresh"), buf);

    ui::footer(TR("A/B:更新 A長押し:戻る", "A/B:Refresh Hold A:Back"));
    g.update(Update::Text);
  }
};

InfoApp gApp;

}  // namespace

App& infoApp() { return gApp; }

}  // namespace pdos
