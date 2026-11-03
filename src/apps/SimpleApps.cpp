// 小さなアプリをまとめたもの。Badger の badge.py / list.py / qrgen.py /
// image.py / fonts.py / help.py に当たる。置き場所も Badger に合わせてある。
//
//   /badges/*.txt      名札（1 行ずつ: 会社 / 名前 / 見出し1 / 内容1 / 見出し2 / 内容2 / 画像）
//   /lists/checklist.txt  チェックリスト（1 行 1 項目）
//   /qrcodes/*.txt     QR（1 行目が中身、2 行目が題、3 行目以降が説明）
//   /images/*          画像（JPEG / PNG / BMP）

#include <Arduino.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "os/I18n.h"
#include "Apps.h"
#include "os/Icons.h"
#include "os/Store.h"
#include "os/TextFile.h"
#include "os/Ui.h"
#include "reader/NaturalOrder.h"

namespace pdos {
namespace {

constexpr uint16_t kMaxFiles = 64;

// フォルダの中から拡張子で拾い、自然な順に並べる。名前は PSRAM に置く。
class FileList {
 public:
  using Name = char[96];

  uint16_t scan(const char* dir, const char* const* exts, size_t extCount) {
    snprintf(dir_, sizeof(dir_), "%s", dir);
    count_ = 0;
    if (names_ == nullptr) {
      names_ = static_cast<Name*>(heap_caps_malloc(sizeof(Name) * kMaxFiles, MALLOC_CAP_SPIRAM));
      if (names_ == nullptr) return 0;
    }
    auto& sd = SDCardManager::getInstance();
    FsFile folder = sd.open(dir, O_RDONLY);
    if (!folder) return 0;
    FsFile entry;
    while (count_ < kMaxFiles && entry.openNext(&folder, O_RDONLY)) {
      char name[96];
      entry.getName(name, sizeof(name));
      const bool isDir = entry.isDirectory();
      entry.close();
      if (isDir || name[0] == '.') continue;
      const char* ext = strrchr(name, '.');
      if (ext == nullptr) continue;
      for (size_t i = 0; i < extCount; ++i) {
        if (strcasecmp(ext, exts[i]) == 0) {
          memcpy(names_[count_++], name, sizeof(Name));
          break;
        }
      }
    }
    for (uint16_t i = 1; i < count_; ++i) {
      for (uint16_t j = i; j > 0 && paperdiy::naturalLess(names_[j], names_[j - 1]); --j) {
        Name tmp;
        memcpy(tmp, names_[j], sizeof(Name));
        memcpy(names_[j], names_[j - 1], sizeof(Name));
        memcpy(names_[j - 1], tmp, sizeof(Name));
      }
    }
    return count_;
  }

  uint16_t count() const { return count_; }
  const char* name(uint16_t i) const { return names_[i]; }
  void path(uint16_t i, char* out, size_t cap) const { snprintf(out, cap, "%s/%s", dir_, names_[i]); }

 private:
  Name* names_ = nullptr;
  uint16_t count_ = 0;
  char dir_[32] = {0};
};

void needSd() { ui::message(TR("SD カードが必要です", "SD card required")); }

// --- 名札 ---------------------------------------------------------------------

class BadgeApp : public App {
 public:
  const char* id() const override { return "badge"; }
  const char* name() const override { return TR("名札", "Badge"); }
  const uint8_t* icon() const override { return icons::badge; }

  void start(bool, Key wake) override {
    if (!os.sdReady()) return needSd();
    static const char* const kExt[] = {".txt"};
    files_.scan("/badges", kExt, 1);
    store::load("badge", index_);
    if (wake == Key::A) step(-1, false);
    if (wake == Key::B) step(1, false);
    render();
  }

  void key(Key k) override {
    if (k == Key::A) step(-1, true);
    if (k == Key::B) step(1, true);
    if (k == Key::LongA) os.goHome();
  }

  void suspend() override { store::save("badge", index_); }

 private:
  void step(int16_t d, bool draw) {
    if (files_.count() == 0) return;
    index_ = static_cast<int16_t>((index_ + d + files_.count()) % files_.count());
    if (draw) render();
  }

  void render() {
    Gfx& g = os.gfx();
    if (files_.count() == 0) {
      ui::message(TR("名札がありません", "No badges"), TR("/badges/badge.txt を置いてください", "Put /badges/badge.txt"));
      return;
    }
    if (index_ >= files_.count()) index_ = 0;
    char path[128];
    files_.path(static_cast<uint16_t>(index_), path, sizeof(path));
    char lines[7][96] = {};
    textfile::Reader r;
    if (r.open(path)) {
      for (auto& line : lines) {
        if (!r.readLine(line, sizeof(line))) break;
      }
    }

    g.clear();
    // 上の帯に会社名。
    g.setPen(15);
    g.fill(0, 0, Gfx::W, 72);
    g.setPen(0);
    g.textCentered(lines[0], Gfx::W / 2, 50, 34);
    g.invert(0, 0, Gfx::W, 72);

    // 写真。無ければ枠だけ。
    constexpr int16_t kPhotoW = 360, kPhotoH = 440;
    const int16_t px = (Gfx::W - kPhotoW) / 2, py = 100;
    uint8_t* photo = lines[6][0] != '\0' ? ui::decodeImage(lines[6], kPhotoW, kPhotoH) : nullptr;
    if (photo != nullptr) {
      g.gray(photo, kPhotoW, kPhotoH, px, py);
      heap_caps_free(photo);
    } else {
      g.setPen(0);
      g.frame(px, py, kPhotoW, kPhotoH, 2);
      g.textCentered(TR("写真なし", "No photo"), Gfx::W / 2, py + kPhotoH / 2, 24);
    }

    g.setPen(0);
    int16_t em = 64;
    while (em > 32 && g.textWidth(lines[1], em) > Gfx::W - 32) em -= 4;
    g.textCentered(lines[1], Gfx::W / 2, 620, em);

    int16_t y = 700;
    for (int i = 2; i + 1 < 6; i += 2) {
      g.setPen(0);
      g.text(lines[i], ui::kMargin, y, 24);
      g.textRight(lines[i + 1], Gfx::W - ui::kMargin, y, 30);
      g.hline(ui::kMargin, y + 16, Gfx::W - ui::kMargin * 2, 2);
      y += 90;
    }
    // 写真があるので階調のきれいなモードで送る。
    g.update(photo != nullptr ? Update::Quality : Update::Text);
  }

  FileList files_;
  int16_t index_ = 0;
};

// --- チェックリスト -----------------------------------------------------------

class ListApp : public App {
 public:
  const char* id() const override { return "list"; }
  const char* name() const override { return TR("一覧", "List"); }
  const uint8_t* icon() const override { return icons::list; }

  void start(bool, Key wake) override {
    if (!os.sdReady()) return needSd();
    if (items_ == nullptr) {
      items_ = static_cast<Item*>(heap_caps_malloc(sizeof(Item) * kMaxItems, MALLOC_CAP_SPIRAM));
    }
    count_ = 0;
    textfile::Reader r;
    if (items_ != nullptr && r.open(kPath)) {
      char line[sizeof(Item)];
      while (count_ < kMaxItems && r.readLine(line, sizeof(line))) {
        textfile::trim(line);
        if (line[0] != '\0') memcpy(items_[count_++], line, sizeof(Item));
      }
    }
    store::load("list", state_);
    list_.reset(count_, state_.cursor);
    if (wake == Key::A) list_.up();
    if (wake == Key::B) list_.down();
    render(Update::Text);
  }

  void key(Key k) override {
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
        if (count_ > 0) {
          state_.checks ^= 1ULL << list_.selected();
          store::save("list", state_);
          render(Update::Fast);
        }
        break;
      case Key::LongA:
        os.goHome();
        break;
      default:
        break;
    }
  }

  void suspend() override {
    state_.cursor = list_.selected();
    store::save("list", state_);
  }

 private:
  static constexpr const char* kPath = "/lists/checklist.txt";
  static constexpr uint16_t kMaxItems = 64;  // 印を 64bit で持つ
  using Item = char[128];
  struct State {
    uint64_t checks = 0;
    int16_t cursor = 0;
  };

  static void label(uint16_t i, char* buf, size_t cap, void* self) {
    const auto* app = static_cast<ListApp*>(self);
    snprintf(buf, cap, "%s %s", (app->state_.checks >> i) & 1 ? "■" : "□", app->items_[i]);
  }

  void render(Update mode) {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("一覧", "List"));
    if (count_ == 0) {
      g.setPen(0);
      g.textCentered(TR("項目がありません", "No items"), Gfx::W / 2, Gfx::H / 2, 28);
      g.textCentered(kPath, Gfx::W / 2, Gfx::H / 2 + 44, 20);
    } else {
      list_.draw(label, this);
    }
    ui::footer(TR("A:上 B:下 B長押し:印 A長押し:戻る", "A:Up B:Down Hold B:Check Hold A:Back"));
    g.update(mode);
  }

  Item* items_ = nullptr;
  uint16_t count_ = 0;
  State state_;
  ui::ListView list_;
};

// --- QR コード ----------------------------------------------------------------

class QrApp : public App {
 public:
  const char* id() const override { return "qrgen"; }
  const char* name() const override { return "QR"; }
  const uint8_t* icon() const override { return icons::qr; }

  void start(bool, Key wake) override {
    if (!os.sdReady()) return needSd();
    static const char* const kExt[] = {".txt"};
    files_.scan("/qrcodes", kExt, 1);
    store::load("qrgen", index_);
    if (files_.count() > 0) {
      if (wake == Key::A) index_ = static_cast<int16_t>((index_ + files_.count() - 1) % files_.count());
      if (wake == Key::B) index_ = static_cast<int16_t>((index_ + 1) % files_.count());
    }
    render();
  }

  void key(Key k) override {
    if (files_.count() > 0 && (k == Key::A || k == Key::B)) {
      const int16_t d = k == Key::A ? files_.count() - 1 : 1;
      index_ = static_cast<int16_t>((index_ + d) % files_.count());
      render();
    }
    if (k == Key::LongA) os.goHome();
  }

  void suspend() override { store::save("qrgen", index_); }

 private:
  void render() {
    Gfx& g = os.gfx();
    if (files_.count() == 0) {
      ui::message(TR("QR の元がありません", "No QR sources"), TR("/qrcodes/*.txt を置いてください", "Put /qrcodes/*.txt"));
      return;
    }
    if (index_ >= files_.count()) index_ = 0;
    char path[128];
    files_.path(static_cast<uint16_t>(index_), path, sizeof(path));
    char data[512] = "";
    char line[160] = "";
    g.clear();
    ui::header("QR");
    textfile::Reader r;
    int16_t y = 660;
    if (r.open(path) && r.readLine(data, sizeof(data))) {
      const int16_t size = Gfx::W - ui::kMargin * 2;
      if (ui::qr(data, ui::kMargin, ui::kHeaderH + 24, size) == 0) {
        g.setPen(0);
        g.textCentered(TR("長すぎて QR にできません", "Too long for a QR code"), Gfx::W / 2, 320, 24);
      }
      if (r.readLine(line, sizeof(line))) {
        g.setPen(0);
        g.textCentered(line, Gfx::W / 2, y, 36);
        y += 56;
      }
      while (r.readLine(line, sizeof(line))) {
        g.setPen(0);
        y = g.textWrap(line, ui::kMargin, y, Gfx::W - ui::kMargin * 2, 24);
      }
    }
    char hint[48];
    snprintf(hint, sizeof(hint), TR("A:前 B:次 A長押し:戻る  %d/%u", "A:Prev B:Next Hold A:Back  %d/%u"), index_ + 1, files_.count());
    ui::footer(hint);
    g.update(Update::Text);
  }

  FileList files_;
  int16_t index_ = 0;
};

// --- 画像 ---------------------------------------------------------------------

class ImageApp : public App {
 public:
  const char* id() const override { return "image"; }
  const char* name() const override { return TR("画像", "Images"); }
  const uint8_t* icon() const override { return icons::image; }

  void start(bool, Key wake) override {
    if (!os.sdReady()) return needSd();
    static const char* const kExt[] = {".jpg", ".jpeg", ".png", ".bmp"};
    files_.scan("/images", kExt, 4);
    store::load("image", state_);
    if (files_.count() > 0) {
      if (wake == Key::A) state_.index = static_cast<int16_t>((state_.index + files_.count() - 1) % files_.count());
      if (wake == Key::B) state_.index = static_cast<int16_t>((state_.index + 1) % files_.count());
    }
    render();
  }

  void key(Key k) override {
    if (files_.count() > 0 && (k == Key::A || k == Key::B)) {
      const int16_t d = k == Key::A ? files_.count() - 1 : 1;
      state_.index = static_cast<int16_t>((state_.index + d) % files_.count());
      render();
    }
    if (k == Key::LongB) {
      state_.landscape = !state_.landscape;
      render();
    }
    if (k == Key::LongA) os.goHome();
  }

  void suspend() override { store::save("image", state_); }

 private:
  struct State {
    int16_t index = 0;
    bool landscape = false;
  };

  void render() {
    Gfx& g = os.gfx();
    if (files_.count() == 0) {
      ui::message(TR("画像がありません", "No images"), TR("/images に JPEG / PNG / BMP を置いてください", "Put JPEG / PNG / BMP in /images"));
      return;
    }
    if (state_.index >= files_.count()) state_.index = 0;
    char path[128];
    files_.path(static_cast<uint16_t>(state_.index), path, sizeof(path));
    g.clear();
    // 横向きはパネルそのままの向きなので、キャンバスへ直接写す。
    const int16_t w = state_.landscape ? Gfx::kPanelW : Gfx::W;
    const int16_t h = state_.landscape ? Gfx::kPanelH : Gfx::H;
    uint8_t* pixels = ui::decodeImage(path, w, h);
    if (pixels == nullptr) {
      g.setPen(0);
      g.textCentered(TR("この画像は読めません", "Cannot read this image"), Gfx::W / 2, Gfx::H / 2, 28);
      g.textCentered(TR("プログレッシブ JPEG は非対応です", "Progressive JPEG is not supported"), Gfx::W / 2, Gfx::H / 2 + 44, 20);
      g.textCentered(files_.name(static_cast<uint16_t>(state_.index)), Gfx::W / 2, Gfx::H / 2 + 88, 18);
      g.update(Update::Text);
      return;
    }
    if (state_.landscape) {
      memcpy(g.canvas(), pixels, static_cast<size_t>(w) * h);
    } else {
      g.gray(pixels, w, h, 0, 0);
    }
    heap_caps_free(pixels);
    g.update(Update::Quality);
  }

  FileList files_;
  State state_;
};

// --- 文字 ---------------------------------------------------------------------

// 文字の大きさと、16 階調の見本。縦書きの見本も出す。
class FontsApp : public App {
 public:
  const char* id() const override { return "fonts"; }
  const char* name() const override { return TR("文字", "Fonts"); }
  const uint8_t* icon() const override { return icons::fonts; }

  void start(bool, Key) override { render(); }

  void key(Key k) override {
    if (k == Key::A) {
      page_ = (page_ + kPages - 1) % kPages;
      render();
    }
    if (k == Key::B) {
      page_ = (page_ + 1) % kPages;
      render();
    }
    if (k == Key::LongA) os.goHome();
  }

 private:
  static constexpr int kPages = 3;

  void render() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("文字", "Fonts"));
    g.setPen(0);
    if (page_ == 0) sizes();
    if (page_ == 1) levels();
    if (page_ == 2) vertical();
    ui::footer(TR("A:前 B:次 A長押し:戻る", "A:Prev B:Next Hold A:Back"));
    g.update(page_ == 1 ? Update::Quality : Update::Text);
  }

  void sizes() {
    Gfx& g = os.gfx();
    static const int16_t kEm[] = {16, 20, 24, 28, 36, 48, 64};
    int16_t y = ui::kHeaderH + 40;
    for (int16_t em : kEm) {
      char line[32];
      snprintf(line, sizeof(line), "%d 文字Abc", em);
      y += em + 12;
      g.setPen(0);
      g.text(line, ui::kMargin, y, em);
    }
    y += 70;
    g.textWrap("いろはにほへと ちりぬるを わかよたれそ つねならむ ABCDEFG abcdefg 0123456789",
               ui::kMargin, y, Gfx::W - ui::kMargin * 2, 26);
  }

  void levels() {
    Gfx& g = os.gfx();
    const int16_t top = ui::kHeaderH + 24;
    const int16_t rowH = (Gfx::H - ui::kFooterH - top - 8) / 16;
    for (int16_t i = 0; i < 16; ++i) {
      g.setPen(static_cast<uint8_t>(i));
      g.fill(100, top + i * rowH, Gfx::W - 124, rowH - 2);
      char n[8];
      snprintf(n, sizeof(n), "%d", i);
      g.setPen(0);
      g.textRight(n, 84, top + i * rowH + rowH - 12, 24);
    }
  }

  // 縦書きの字の並びを見るだけの見本。本文の組版は電子書籍の JaBook が行う。
  void vertical() {
    Gfx& g = os.gfx();
    static const char* const kLines[] = {"吾輩は猫である。", "名前はまだ無い。", "どこで生れたか", "とんと見当がつかぬ。"};
    constexpr int16_t kEm = 40;
    int16_t x = Gfx::W - 90;
    for (const char* line : kLines) {
      g.setPen(0);
      g.textVertical(line, x, ui::kHeaderH + 30, kEm);
      x -= kEm + 40;
    }
  }

  int page_ = 0;
};

// --- 説明 ---------------------------------------------------------------------

class HelpApp : public App {
 public:
  const char* id() const override { return "help"; }
  const char* name() const override { return TR("説明", "Help"); }
  const uint8_t* icon() const override { return icons::help; }

  void start(bool, Key) override { render(); }

  void key(Key k) override {
    if (k == Key::A) {
      page_ = (page_ + kPageCount - 1) % kPageCount;
      render();
    }
    if (k == Key::B) {
      page_ = (page_ + 1) % kPageCount;
      render();
    }
    if (k == Key::LongA) os.goHome();
  }

 private:
  static constexpr const char* kPagesJa[] = {
      "ボタン\n\n"
      "A（上）と B（下）が左の側面にあります。\n\n"
      "A / B を押す: 前へ / 次へ\n"
      "B を長く押す: 決定\n"
      "A を長く押す: 1 つ戻る\n"
      "A と B を一緒に: ランチャーへ直行\n\n"
      "電源ボタン\n\n"
      "1 回: 電源を入れる／リセット\n"
      "2 回: 電源を切る\n"
      "長く: 書き込みモード",

      "眠りと再開\n\n"
      "電池のときは、手が止まると自動で眠ります。"
      "画面は消えません。\n\n"
      "A か B を押すと、眠る前のアプリへ戻ります。"
      "押したボタンの操作もそのまま効きます。\n\n"
      "USB がつながっているあいだは眠りません。\n\n"
      "電源ボタンを 2 回押して切っても、次に入れたとき同じアプリへ戻ります。",

      "SD カードの中身\n\n"
      "/books     本（.epub .txt 青空文庫 .zip）\n"
      "/images    画像（JPEG PNG BMP）\n"
      "/badges    名札（.txt と写真）\n"
      "/lists     チェックリスト\n"
      "/qrcodes   QR の元（.txt）\n"
      "/config    天気・ニュースの設定\n"
      "/examples  Python のアプリ（.py）\n\n"
      "Wi-Fi と時刻は「設定」から変えられます。",

      "MicroPython\n\n"
      "SD の /examples に .py を置くと、ランチャーに並びます。"
      "同じ名前の icon-名前.png がアイコンになります。\n\n"
      "USB につなぐと REPL が使えます。mpremote や Thonny でつなぐと、"
      "今のアプリを閉じて REPL に入ります。A を長く押すと戻ります。\n\n"
      "Badger の badger2040 と同じ書き方が使えます。",
  };
  static constexpr const char* kPagesEn[] = {
      "Buttons\n\n"
      "A (upper) and B (lower) are on the left side.\n\n"
      "Press A / B: previous / next\n"
      "Hold B: OK\n"
      "Hold A: back one step\n"
      "A and B together: straight to launcher\n\n"
      "Power button\n\n"
      "Once: power on / reset\n"
      "Twice: power off\n"
      "Hold: download mode",

      "Sleep and resume\n\n"
      "On battery the device sleeps when left alone. The screen stays on.\n\n"
      "Press A or B to return to the app you were using. "
      "The press also counts as an input.\n\n"
      "It does not sleep while USB is connected.\n\n"
      "After a power off, it returns to the same app the next time.",

      "On the SD card\n\n"
      "/books     .epub .txt Aozora .zip\n"
      "/images    JPEG PNG BMP\n"
      "/badges    badge .txt and photo\n"
      "/lists     checklist\n"
      "/qrcodes   QR sources (.txt)\n"
      "/config    weather and news settings\n"
      "/examples  Python apps (.py)\n\n"
      "Wi-Fi and the clock are in Settings.",

      "MicroPython\n\n"
      "Put a .py file in /examples and it appears in the launcher. "
      "icon-name.png next to it becomes the icon.\n\n"
      "Over USB there is a REPL. Connecting mpremote or Thonny closes the "
      "current app and opens the REPL. Hold A to go back.\n\n"
      "The badger2040 API from Badger OS works here too.",
  };
  static constexpr int kPageCount = sizeof(kPagesJa) / sizeof(kPagesJa[0]);

  void render() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("説明", "Help"));
    g.setPen(0);
    const char* text = i18n::lang() == i18n::Lang::Ja ? kPagesJa[page_] : kPagesEn[page_];
    g.textWrap(text, ui::kMargin, ui::kHeaderH + 50, Gfx::W - ui::kMargin * 2, 26, 40);
    char hint[64];
    snprintf(hint, sizeof(hint), TR("A:前 B:次 A長押し:戻る  %d/%d", "A:Prev B:Next Hold A:Back  %d/%d"), page_ + 1, kPageCount);
    ui::footer(hint);
    g.update(Update::Text);
  }

  int page_ = 0;
};

BadgeApp gBadge;
ListApp gList;
QrApp gQr;
ImageApp gImage;
FontsApp gFonts;
HelpApp gHelp;

}  // namespace

App& badgeApp() { return gBadge; }
App& listApp() { return gList; }
App& qrgenApp() { return gQr; }
App& imageApp() { return gImage; }
App& fontsApp() { return gFonts; }
App& helpApp() { return gHelp; }

}  // namespace pdos
