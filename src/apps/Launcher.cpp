// ランチャー。Badger の launcher.py に当たる。
//
// 2 列 4 段でアプリを並べ、A / B でカーソルを動かし、B 長押しで開く。
// 本体に組み込んだアプリのあとに、SD の /examples にある .py を並べる
// （Badger と同じ置き場所。icon-名前.png があればアイコンにする）。
// カーソルの移動は Fast で済ませるので、灰色は使わず白黒だけで描く。

#include <SDCardManager.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "os/I18n.h"
#include "os/Icons.h"
#include "os/Os.h"
#include "os/Store.h"
#include "os/Ui.h"
#include "reader/NaturalOrder.h"

namespace pdos {
namespace {

constexpr int16_t kCols = 2;
constexpr int16_t kRows = 4;
constexpr int16_t kPerPage = kCols * kRows;
constexpr int16_t kTop = 60;
constexpr int16_t kFooterTop = Gfx::H - 86;
constexpr int16_t kCellW = Gfx::W / kCols;
constexpr int16_t kCellH = (kFooterTop - kTop) / kRows;
constexpr const char* kStateKey = "launcher";
constexpr int16_t kCleanEvery = 30;
constexpr const char* kAppDir = "/examples";
constexpr uint16_t kMaxScripts = 48;
// SD の使用量を測るのは、手が止まってこれだけたってから。
constexpr uint32_t kDiskScanDelayMs = 5000;

struct LauncherState {
  int16_t cursor = 0;
  int16_t usedPermille = -1;  // SD の使用率。まだ測っていなければ -1
};

// /examples の .py 1 本ぶん。
struct Script {
  char file[40];     // hello.py
  char label[40];    // hello
  uint8_t* icon;     // 8bit グレー。まだ読んでいなければ nullptr
  bool iconChecked;  // 読みに行ったか
};

class Launcher : public App {
 public:
  const char* id() const override { return "launcher"; }
  const char* name() const override { return TR("ランチャー", "Launcher"); }
  const uint8_t* icon() const override { return nullptr; }

  void start(bool resumed, Key) override {
    scan();
    store::load(kStateKey, state_);
    if (state_.cursor < 0 || state_.cursor >= total()) state_.cursor = 0;
    page_ = state_.cursor / kPerPage;
    render(Update::Text);
    // 他のアプリが書いたかもしれないので測り直す。眠りから戻っただけなら前の値で足りる。
    diskDirty_ = os.sdReady() && (!resumed || state_.usedPermille < 0);
    if (diskDirty_) os.setTick(kDiskScanDelayMs);
  }

  // SD の使用量は FAT を全部読むので、8GB のカードで 4 秒ほどかかる。描画の中で
  // 測ると移動が止まって見えるので、手が止まっているときに測って値を控える。
  void tick() override {
    if (!diskDirty_) return;
    diskDirty_ = false;
    auto& sd = SDCardManager::getInstance();
    const uint64_t total = sd.sdTotalBytes();
    if (total == 0) return;
    const int16_t permille = static_cast<int16_t>(sd.sdUsedBytes() * 1000 / total);
    if (permille == state_.usedPermille) return;
    state_.usedPermille = permille;
    store::save(kStateKey, state_);
    render(Update::Fast);
  }

  void key(Key k) override {
    if (diskDirty_) os.setTick(kDiskScanDelayMs);  // 手が動いているあいだは測らない
    switch (k) {
      case Key::A:
        move(-1);
        break;
      case Key::B:
        move(1);
        break;
      case Key::LongB:
        open();
        break;
      default:
        break;
    }
  }

  void suspend() override { store::save(kStateKey, state_); }

 private:
  int16_t total() const { return static_cast<int16_t>(kAppCount + scriptCount_); }

  void open() {
    store::save(kStateKey, state_);
    if (state_.cursor < kAppCount) {
      os.launch(kApps[state_.cursor]->id());
      return;
    }
    char id[96];
    snprintf(id, sizeof(id), "py:%s/%s", kAppDir, scripts_[state_.cursor - kAppCount].file);
    os.launch(id);
  }

  void scan() {
    if (scripts_ == nullptr) {
      scripts_ = static_cast<Script*>(heap_caps_malloc(sizeof(Script) * kMaxScripts, MALLOC_CAP_SPIRAM));
      if (scripts_ == nullptr) return;
    }
    for (uint16_t i = 0; i < scriptCount_; ++i) heap_caps_free(scripts_[i].icon);
    scriptCount_ = 0;
    if (!os.sdReady()) return;
    FsFile dir = SDCardManager::getInstance().open(kAppDir, O_RDONLY);
    if (!dir) return;
    FsFile entry;
    while (scriptCount_ < kMaxScripts && entry.openNext(&dir, O_RDONLY)) {
      char name[64];
      entry.getName(name, sizeof(name));
      const bool isDir = entry.isDirectory();
      entry.close();
      const size_t len = strlen(name);
      if (isDir || name[0] == '.' || len < 4 || len >= sizeof(Script::file) || strcasecmp(name + len - 3, ".py") != 0) continue;
      Script& s = scripts_[scriptCount_++];
      snprintf(s.file, sizeof(s.file), "%s", name);
      snprintf(s.label, sizeof(s.label), "%.*s", static_cast<int>(len - 3), name);
      for (char* p = s.label; *p != '\0'; ++p) {
        if (*p == '_') *p = ' ';
      }
      s.icon = nullptr;
      s.iconChecked = false;
    }
    for (uint16_t i = 1; i < scriptCount_; ++i) {
      for (uint16_t j = i; j > 0 && paperdiy::naturalLess(scripts_[j].file, scripts_[j - 1].file); --j) {
        const Script tmp = scripts_[j];
        scripts_[j] = scripts_[j - 1];
        scripts_[j - 1] = tmp;
      }
    }
  }

  void move(int16_t delta) {
    state_.cursor = static_cast<int16_t>((state_.cursor + delta + total()) % total());
    page_ = state_.cursor / kPerPage;
    // モードを切り替えると全画素が動いて点滅するので、ふだんは Fast で通し、
    // 残像がたまる前にときどき Text で掃除する。
    render(++fastCount_ >= kCleanEvery ? Update::Text : Update::Fast);
    if (fastCount_ >= kCleanEvery) fastCount_ = 0;
  }

  const uint8_t* scriptIcon(Script& s) {
    if (!s.iconChecked) {
      s.iconChecked = true;
      char stem[40];
      snprintf(stem, sizeof(stem), "%.*s", static_cast<int>(strlen(s.file) - 3), s.file);
      for (char* p = stem; *p != '\0'; ++p) {
        if (*p == '_') *p = '-';  // Badger と同じく icon-net-info.png
      }
      for (const char* ext : {"png", "jpg"}) {
        char path[80];
        snprintf(path, sizeof(path), "%s/icon-%s.%s", kAppDir, stem, ext);
        if (SDCardManager::getInstance().exists(path)) {
          s.icon = ui::decodeImage(path, icons::kSize, icons::kSize);
          if (s.icon != nullptr) break;
        }
      }
    }
    return s.icon;
  }

  void render(Update mode) {
    Gfx& g = os.gfx();
    g.clear();
    ui::header("PaperDIY OS");

    const int16_t first = page_ * kPerPage;
    for (int16_t i = 0; i < kPerPage && first + i < total(); ++i) {
      const int16_t index = first + i;
      const int16_t x = (i % kCols) * kCellW;
      const int16_t y = kTop + (i / kCols) * kCellH;
      const int16_t ix = x + (kCellW - icons::kSize) / 2, iy = y + 16;
      const char* label;
      g.setPen(0);
      if (index < kAppCount) {
        App* app = kApps[index];
        label = app->name();
        if (app->icon() != nullptr) g.bitmap(app->icon(), icons::kSize, icons::kSize, ix, iy);
      } else {
        Script& s = scripts_[index - kAppCount];
        label = s.label;
        const uint8_t* gray = scriptIcon(s);
        if (gray != nullptr) {
          g.gray(gray, icons::kSize, icons::kSize, ix, iy);
        } else {
          g.bitmap(icons::code, icons::kSize, icons::kSize, ix, iy);
        }
      }
      g.setPen(0);
      g.textCentered(label, x + kCellW / 2, y + kCellH - 30, 28);
      if (index == state_.cursor) g.invert(x + 8, y + 4, kCellW - 16, kCellH - 8);
    }

    drawPageDots();
    drawDiskUsage();
    ui::footer(TR("A:前 B:次 B長押し:開く", "A:Prev B:Next Hold B:Open"));
    g.update(mode);
  }

  // ページの印。Badger の右端の四角に当たる。
  void drawPageDots() {
    Gfx& g = os.gfx();
    const int16_t pages = (total() + kPerPage - 1) / kPerPage;
    if (pages < 2) return;
    const int16_t size = 12, gap = 8;
    int16_t x = (Gfx::W - (pages * size + (pages - 1) * gap)) / 2;
    const int16_t y = kFooterTop + 8;
    for (int16_t p = 0; p < pages; ++p) {
      g.setPen(0);
      if (p == page_) {
        g.fill(x, y, size, size);
      } else {
        g.frame(x, y, size, size, 2);
      }
      x += size + gap;
    }
  }

  // SD の使用量を細く出す（Badger の見出しにある使用率の棒）。
  void drawDiskUsage() {
    Gfx& g = os.gfx();
    if (!os.sdReady()) {
      g.setPen(0);
      g.text(TR("SD なし", "No SD"), ui::kMargin, kFooterTop + 20, 18);
      return;
    }
    const int16_t w = 120, h = 12;
    const int16_t x = Gfx::W - ui::kMargin - w, y = kFooterTop + 8;
    g.setPen(0);
    g.frame(x, y, w, h, 2);
    if (state_.usedPermille >= 0) {
      g.fill(x + 2, y + 2, static_cast<int16_t>((w - 4) * state_.usedPermille / 1000), h - 4);
    }
  }

  LauncherState state_;
  int16_t page_ = 0;
  int16_t fastCount_ = 0;
  bool diskDirty_ = false;
  Script* scripts_ = nullptr;
  uint16_t scriptCount_ = 0;
};

Launcher gLauncher;

}  // namespace

App* launcherApp() { return &gLauncher; }

}  // namespace pdos
