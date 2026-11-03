// 電子書籍リーダ。Badger の ebook.py に当たる。
//
// 組版は縦組みが JaBook、横組みが FreeInkBook の ChapterLayout（src/reader）。
// ここは画面の行き来と 2 ボタンの割り当てだけを受け持つ。
//
//   読書中   A:前のページ  B:次のページ  B 長押し:メニュー  A 長押し:本棚
//   一覧     A:上  B:下  B 長押し:決定  A 長押し:戻る
//
// A 長押しは常に 1 つ上へ戻る。ランチャー > 本棚 > 読書 > メニュー > 目次など。
//
// 読書位置は文字位置で覚えているので、文字の大きさや組み方を変えても同じ
// 一文から続けられる。眠って起きたときは同じ本を開き直し、起こしたボタンの
// ぶんだけページを送ってから描く。

#include <Arduino.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

#include "os/I18n.h"
#include "Apps.h"
#include "os/Icons.h"
#include "os/Store.h"
#include "os/Ui.h"
#include "reader/Aozora.h"
#include "reader/Bookmarks.h"
#include "reader/Config.h"
#include "reader/FontStore.h"
#include "reader/Library.h"
#include "reader/ReaderEngine.h"
#include "reader/RecentBooks.h"

namespace pdos {
namespace {

using paperdiy::Bookmark;
using paperdiy::BookPosition;
using paperdiy::Bookmarks;
using paperdiy::Library;
using paperdiy::ReaderEngine;
using paperdiy::ReaderSettings;
using paperdiy::RecentBooks;
using paperdiy::kFontSizeCount;
using paperdiy::kFontSizes;
using paperdiy::kLineGapCount;
using paperdiy::kLineGaps;
using paperdiy::kMarginCount;
using paperdiy::kMargins;

constexpr const char* kStateKey = "ebook";
constexpr uint32_t kProgressRedrawMs = 1500;
constexpr uint8_t kGotoStep = 5;

enum class Screen : uint8_t { Library, Reader, Menu, Toc, Marks, Goto, End };

enum MenuItem : int16_t {
  MenuResume,
  MenuToc,
  MenuMark,
  MenuMarks,
  MenuGoto,
  MenuWriting,
  MenuFontSize,
  MenuLineGap,
  MenuMargin,
  MenuRuby,
  MenuKinsoku,
  MenuTcy,
  MenuTurn,
  MenuLibrary,
  MenuCount,
};

struct SavedState {
  char path[192] = {0};
  ReaderSettings settings;
  bool cleanTurn = false;  // true なら毎回きれいにめくる（遅い）
};

ReaderEngine* gEngine = nullptr;
Library* gLibrary = nullptr;
Bookmarks gMarks;
RecentBooks gRecent;
SavedState gSaved;
uint32_t gLastProgressMs = 0;

void onBuildProgress(const char* label, uint32_t pages, void*) {
  // 組版は長いと数十秒かかる。止まって見えないように、ときどき数字を出す。
  if (millis() - gLastProgressMs < kProgressRedrawMs) return;
  gLastProgressMs = millis();
  char line[64];
  if (pages == 0) {
    snprintf(line, sizeof(line), "%s…", label);
  } else {
    snprintf(line, sizeof(line), TR("%s… %u ページ", "%s... %u pages"), label, static_cast<unsigned>(pages));
  }
  Gfx& g = os.gfx();
  g.clear();
  g.setPen(0);
  g.textCentered(line, Gfx::W / 2, Gfx::H / 2, 28);
  g.update(Update::Fast);
}

void onConvertProgress(void*, const char* label, uint8_t percent) {
  char line[96];
  snprintf(line, sizeof(line), "%s %u%%", label, static_cast<unsigned>(percent));
  onBuildProgress(line, 0, nullptr);
}

class EbookApp : public App {
 public:
  const char* id() const override { return "ebook"; }
  const char* name() const override { return TR("電子書籍", "E-book"); }
  const uint8_t* icon() const override { return icons::ebook; }

  void start(bool, Key wake) override {
    if (!os.sdReady()) {
      ui::message(TR("SD カードが必要です", "SD card required"), TR("/books に本を入れてください", "Put books in /books"));
      return;
    }
    if (!os.gfx().hasFonts()) return;
    if (!prepare()) {
      ui::message(TR("メモリが足りません", "Out of memory"));
      return;
    }
    if (gSaved.path[0] != '\0' && open(gSaved.path)) {
      if (wake == Key::A) gEngine->prevPage();
      if (wake == Key::B) gEngine->nextPage();
      enterReader(true);
      return;
    }
    enterLibrary();
  }

  void key(Key k) override {
    if (gEngine == nullptr) {
      if (k == Key::LongA) os.goHome();
      return;
    }
    switch (screen_) {
      case Screen::Reader:
        readerKey(k);
        break;
      case Screen::Library:
        libraryKey(k);
        break;
      case Screen::Menu:
        menuKey(k);
        break;
      case Screen::Toc:
        tocKey(k);
        break;
      case Screen::Marks:
        marksKey(k);
        break;
      case Screen::Goto:
        gotoKey(k);
        break;
      case Screen::End:
        endKey(k);
        break;
    }
  }

  void suspend() override {
    if (gEngine != nullptr && gEngine->isOpen()) gEngine->saveProgress();
    store::save(kStateKey, gSaved);
  }

  void stop() override {
    if (gEngine != nullptr && gEngine->isOpen()) {
      gEngine->closeBook();
      gMarks.close();
    }
  }

 private:
  bool prepare() {
    store::load(kStateKey, gSaved);
    if (gEngine != nullptr) {
      gEngine->applySettings(gSaved.settings);
      return true;
    }
    // 大きいので PSRAM に置く。一度作ったら使い回す。
    void* em = heap_caps_malloc(sizeof(ReaderEngine), MALLOC_CAP_SPIRAM);
    void* lm = heap_caps_malloc(sizeof(Library), MALLOC_CAP_SPIRAM);
    if (em == nullptr || lm == nullptr) return false;
    gEngine = new (em) ReaderEngine();
    gLibrary = new (lm) Library();
    if (!gEngine->begin(os.fonts())) return false;
    gEngine->setProgressCallback(onBuildProgress, nullptr);
    gEngine->applySettings(gSaved.settings);

    static freeink::book::Arena work;
    work.init(heap_caps_malloc(256u * 1024, MALLOC_CAP_SPIRAM), 256u * 1024);
    gLibrary->begin(&work);
    gRecent.load();
    return true;
  }

  bool open(const char* path) {
    if (!SDCardManager::getInstance().exists(path)) {
      gSaved.path[0] = '\0';
      return false;
    }
    gLastProgressMs = 0;
    if (!gEngine->openBook(path)) {
      gSaved.path[0] = '\0';
      return false;
    }
    gMarks.open(gEngine->bookId());
    gRecent.add(gEngine->path(), gEngine->title());
    snprintf(gSaved.path, sizeof(gSaved.path), "%s", path);
    store::save(kStateKey, gSaved);
    return true;
  }

  // --- 読書 ------------------------------------------------------------------

  void enterReader(bool clean) {
    screen_ = Screen::Reader;
    turnsSinceClean_ = 0;
    paint(clean);
  }

  void paint(bool clean) {
    Gfx& g = os.gfx();
    if (!gEngine->hasContent() && !gEngine->onCoverPage()) {
      ui::message(TR("この本に表示できる本文が", "This book has no"), TR("見つかりません", "text to display"));
      return;
    }
    if (!gEngine->renderPage(g.canvas())) {
      ui::message(TR("ページを読み出せません", "Cannot read the page"));
      return;
    }
    if (!gEngine->onCoverPage()) drawStatus();  // 扉の枠に重ねない
    const bool full = clean || gSaved.cleanTurn || ++turnsSinceClean_ >= kCleanEvery;
    if (full) turnsSinceClean_ = 0;
    g.update(full ? Update::Text : Update::Fast);
  }

  void drawStatus() {
    Gfx& g = os.gfx();
    const uint16_t permille = gEngine->progressPermille();
    char right[48];
    const int16_t pct = os.batteryPercent();
    if (pct >= 0) {
      snprintf(right, sizeof(right), TR("%u.%u%%  電池 %d%%", "%u.%u%%  Battery %d%%"), permille / 10, permille % 10, pct);
    } else {
      snprintf(right, sizeof(right), "%u.%u%%", permille / 10, permille % 10);
    }
    g.setPen(0);
    g.textRight(right, Gfx::W - 18, Gfx::H - 14, 14);
    const BookPosition pos = gEngine->position();
    if (gMarks.find(pos.spine, pos.charOffset) >= 0) g.text(TR("しおり", "Bookmark"), 18, Gfx::H - 14, 14);
  }

  void readerKey(Key k) {
    switch (k) {
      case Key::A:
        if (gEngine->prevPage()) {
          gEngine->saveProgress();
          paint(false);
        }
        break;
      case Key::B:
        if (gEngine->nextPage()) {
          gEngine->saveProgress();
          paint(false);
        } else if (gEngine->atEnd()) {
          enterEnd();
        }
        break;
      case Key::LongB:
        enterMenu();
        break;
      case Key::LongA:
        gEngine->saveProgress();
        enterLibrary();
        break;
      default:
        break;
    }
  }

  // --- 本の終わり ------------------------------------------------------------

  void enterEnd() {
    screen_ = Screen::End;
    Gfx& g = os.gfx();
    g.clear();
    ui::header(gEngine->title());
    g.setPen(0);
    g.textCentered(TR("おわり", "The End"), Gfx::W / 2, Gfx::H / 2, 48);
    ui::footer(TR("B長押し:本棚 A長押し:戻る", "Hold B:Library Hold A:Back"));
    g.update(Update::Fast);
  }

  void endKey(Key k) {
    if (k == Key::A || k == Key::B) enterReader(false);
    if (k == Key::LongA) enterReader(false);
    if (k == Key::LongB) enterLibrary();
  }

  // --- 本棚 ------------------------------------------------------------------

  void enterLibrary() {
    screen_ = Screen::Library;
    if (gLibrary->currentDir()[0] == '\0') {
      ui::message(TR("本棚を読み込んでいます…", "Loading the library..."));
      gLibrary->openDir(nullptr);
    } else {
      gLibrary->refresh();
    }
    list_.reset(gLibrary->count());
    drawLibrary(Update::Text);
  }

  static void libraryLabel(uint16_t index, char* buf, size_t cap, void*) {
    const auto& entry = gLibrary->at(index);
    if (entry.isDir) {
      snprintf(buf, cap, "%s/", entry.name);
    } else {
      snprintf(buf, cap, "%s", entry.title[0] != '\0' ? entry.title : entry.name);
    }
  }

  void drawLibrary(Update mode) {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(gLibrary->dirLabel());
    if (gLibrary->count() == 0) {
      g.setPen(0);
      g.textCentered(TR("本がありません", "No books"), Gfx::W / 2, Gfx::H / 2, 28);
      g.textCentered(TR("/books に .epub か .txt を入れてください", "Put .epub or .txt files in /books"), Gfx::W / 2, Gfx::H / 2 + 44, 20);
    } else {
      list_.draw(libraryLabel, nullptr);
    }
    ui::footer(TR("A:上 B:下 B長押し:開く A長押し:戻る", "A:Up B:Down Hold B:Open Hold A:Back"));
    g.update(mode);
  }

  void libraryKey(Key k) {
    switch (k) {
      case Key::A:
        list_.up();
        drawLibrary(Update::Fast);
        break;
      case Key::B:
        list_.down();
        drawLibrary(Update::Fast);
        break;
      case Key::LongB:
        if (gLibrary->count() > 0) openSelected();
        break;
      case Key::LongA: {
        int16_t selected = 0;
        if (!gLibrary->atRoot() && gLibrary->up(&selected)) {
          list_.reset(gLibrary->count(), selected);
          drawLibrary(Update::Fast);
        } else {
          os.goHome();
        }
        break;
      }
      default:
        break;
    }
  }

  void openSelected() {
    const uint16_t index = static_cast<uint16_t>(list_.selected());
    if (gLibrary->at(index).isDir) {
      gLibrary->enter(index);
      list_.reset(gLibrary->count());
      drawLibrary(Update::Fast);
      return;
    }
    char path[192];
    gLibrary->pathAt(index, path, sizeof(path));

    // 青空文庫のテキスト形式（ZIP）は一度だけ EPUB に組み直して開く。
    if (gLibrary->at(index).isZip) {
      char epub[192];
      snprintf(epub, sizeof(epub), "%s", path);
      snprintf(epub + strlen(epub) - 4, sizeof(epub) - (strlen(epub) - 4), ".epub");
      const bool have = SDCardManager::getInstance().exists(epub);
      if (!have || !paperdiy::aozoraEpubIsCurrent(epub)) {
        if (have) gLibrary->dropBookData(epub);
        gLastProgressMs = 0;
        if (!paperdiy::convertAozoraZip(path, epub, nullptr, onConvertProgress, nullptr)) {
          ui::message(TR("この ZIP は読める形に", "This ZIP could not"), TR("直せませんでした", "be converted"));
          delay(1500);
          drawLibrary(Update::Text);
          return;
        }
      }
      snprintf(path, sizeof(path), "%s", epub);
    }

    if (gEngine->isOpen()) {
      gEngine->saveProgress();
      gEngine->closeBook();
      gMarks.close();
    }
    ui::message(TR("開いています…", "Opening..."));
    if (!open(path)) {
      ui::message(TR("この本は開けませんでした", "Could not open this book"));
      delay(1500);
      drawLibrary(Update::Text);
      return;
    }
    enterReader(true);
  }

  // --- メニュー --------------------------------------------------------------

  void enterMenu() {
    screen_ = Screen::Menu;
    list_.reset(MenuCount);
    settingsDirty_ = false;
    drawMenu(Update::Fast);
  }

  static void menuLabel(uint16_t index, char* buf, size_t cap, void* user) {
    const auto& s = gSaved.settings;
    const char* const kWriting[] = {TR("自動", "Auto"), TR("縦書き", "vertical"), TR("横書き", "horizontal")};
    switch (index) {
      case MenuResume:
        snprintf(buf, cap, TR("読書に戻る", "Back to reading"));
        break;
      case MenuToc:
        snprintf(buf, cap, TR("目次", "Contents"));
        break;
      case MenuMark: {
        const BookPosition pos = gEngine->position();
        snprintf(buf, cap, gMarks.find(pos.spine, pos.charOffset) >= 0 ? TR("しおりを外す", "Remove bookmark") : TR("しおりを挟む", "Add bookmark"));
        break;
      }
      case MenuMarks:
        snprintf(buf, cap, TR("しおりの一覧（%u）", "Bookmarks (%u)"), gMarks.count());
        break;
      case MenuGoto:
        snprintf(buf, cap, TR("位置を指定して移動", "Go to position"));
        break;
      case MenuWriting:
        if (s.writing == paperdiy::WritingAuto) {
          snprintf(buf, cap, TR("組み方: 自動（いまは%s）", "Layout: Auto (now %s)"), gEngine->isVertical() ? TR("縦書き", "vertical") : TR("横書き", "horizontal"));
        } else {
          snprintf(buf, cap, TR("組み方: %s", "Layout: %s"), kWriting[s.writing % 3]);
        }
        break;
      case MenuFontSize:
        snprintf(buf, cap, TR("文字の大きさ: %u", "Font size: %u"), kFontSizes[s.fontSize % kFontSizeCount]);
        break;
      case MenuLineGap:
        snprintf(buf, cap, TR("行間: %u%%", "Line spacing: %u%%"), kLineGaps[s.lineGap % kLineGapCount]);
        break;
      case MenuMargin:
        snprintf(buf, cap, TR("余白: %d", "Margin: %d"), kMargins[s.margin % kMarginCount]);
        break;
      case MenuRuby:
        snprintf(buf, cap, TR("ルビ: %s", "Ruby: %s"), s.ruby ? TR("表示", "show") : TR("隠す", "hide"));
        break;
      case MenuKinsoku:
        snprintf(buf, cap, TR("禁則: %s", "Line breaking: %s"), s.strictKinsoku ? TR("厳格", "strict") : TR("標準", "normal"));
        break;
      case MenuTcy:
        snprintf(buf, cap, TR("縦中横: %s", "Tate-chu-yoko: %s"), s.tateChuYoko ? TR("する", "on") : TR("しない", "off"));
        break;
      case MenuTurn:
        snprintf(buf, cap, TR("めくり: %s", "Page turn: %s"), gSaved.cleanTurn ? TR("きれい（遅い）", "clean (slow)") : TR("速い", "fast"));
        break;
      case MenuLibrary:
        snprintf(buf, cap, TR("本棚へ", "Library"));
        break;
      default:
        break;
    }
    (void)user;
  }

  void drawMenu(Update mode) {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(gEngine->title());
    list_.draw(menuLabel, nullptr);
    ui::footer(TR("A:上 B:下 B長押し:決定 A長押し:戻る", "A:Up B:Down Hold B:OK Hold A:Back"));
    g.update(mode);
  }

  void applySettingsIfChanged() {
    if (!settingsDirty_) return;
    settingsDirty_ = false;
    store::save(kStateKey, gSaved);
    gLastProgressMs = 0;
    ui::message(TR("組み直しています…", "Re-typesetting..."));
    gEngine->applySettings(gSaved.settings);
    os.touch();
  }

  void menuKey(Key k) {
    auto& s = gSaved.settings;
    switch (k) {
      case Key::A:
        list_.up();
        drawMenu(Update::Fast);
        return;
      case Key::B:
        list_.down();
        drawMenu(Update::Fast);
        return;
      case Key::LongA:
        applySettingsIfChanged();
        enterReader(true);
        return;
      case Key::LongB:
        break;
      default:
        return;
    }
    switch (list_.selected()) {
      case MenuResume:
        applySettingsIfChanged();
        enterReader(true);
        return;
      case MenuToc:
        enterToc();
        return;
      case MenuMark:
        toggleMark();
        break;
      case MenuMarks:
        enterMarks();
        return;
      case MenuGoto:
        gotoPercent_ = gEngine->progressPermille() / 10;
        screen_ = Screen::Goto;
        drawGoto();
        return;
      case MenuWriting:
        s.writing = static_cast<uint8_t>((s.writing + 1) % 3);
        settingsDirty_ = true;
        break;
      case MenuFontSize:
        s.fontSize = static_cast<uint8_t>((s.fontSize + 1) % kFontSizeCount);
        settingsDirty_ = true;
        break;
      case MenuLineGap:
        s.lineGap = static_cast<uint8_t>((s.lineGap + 1) % kLineGapCount);
        settingsDirty_ = true;
        break;
      case MenuMargin:
        s.margin = static_cast<uint8_t>((s.margin + 1) % kMarginCount);
        settingsDirty_ = true;
        break;
      case MenuRuby:
        s.ruby = !s.ruby;
        settingsDirty_ = true;
        break;
      case MenuKinsoku:
        s.strictKinsoku = !s.strictKinsoku;
        settingsDirty_ = true;
        break;
      case MenuTcy:
        s.tateChuYoko = !s.tateChuYoko;
        settingsDirty_ = true;
        break;
      case MenuTurn:
        gSaved.cleanTurn = !gSaved.cleanTurn;
        store::save(kStateKey, gSaved);
        break;
      case MenuLibrary:
        applySettingsIfChanged();
        gEngine->saveProgress();
        enterLibrary();
        return;
      default:
        break;
    }
    drawMenu(Update::Fast);
  }

  void toggleMark() {
    Bookmark entry;
    const BookPosition pos = gEngine->position();
    entry.spine = pos.spine;
    entry.charOffset = pos.charOffset;
    entry.permille = gEngine->progressPermille();
    gEngine->pageSummary(entry.summary, sizeof(entry.summary));
    gMarks.toggle(entry);
  }

  // --- 目次 ------------------------------------------------------------------

  void enterToc() {
    screen_ = Screen::Toc;
    list_.reset(gEngine->tocCount());
    drawToc();
  }

  static void tocLabel(uint16_t index, char* buf, size_t cap, void*) {
    snprintf(buf, cap, "%s", gEngine->tocTitle(index));
  }

  void drawToc() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("目次", "Contents"));
    if (list_.count() == 0) {
      g.setPen(0);
      g.textCentered(TR("この本に目次はありません", "This book has no contents"), Gfx::W / 2, Gfx::H / 2, 26);
    } else {
      list_.draw(tocLabel, nullptr);
    }
    ui::footer(TR("A:上 B:下 B長押し:移動 A長押し:戻る", "A:Up B:Down Hold B:Go Hold A:Back"));
    g.update(Update::Fast);
  }

  void tocKey(Key k) {
    switch (k) {
      case Key::A:
        list_.up();
        drawToc();
        break;
      case Key::B:
        list_.down();
        drawToc();
        break;
      case Key::LongB:
        if (list_.count() > 0) {
          gLastProgressMs = 0;
          gEngine->gotoToc(static_cast<uint16_t>(list_.selected()));
          gEngine->saveProgress();
          enterReader(true);
        }
        break;
      case Key::LongA:
        enterMenu();
        break;
      default:
        break;
    }
  }

  // --- しおり ----------------------------------------------------------------

  void enterMarks() {
    screen_ = Screen::Marks;
    list_.reset(gMarks.count());
    drawMarks();
  }

  static void marksLabel(uint16_t index, char* buf, size_t cap, void*) {
    const Bookmark& m = gMarks.at(index);
    snprintf(buf, cap, "%u%%  %s", m.permille / 10, m.summary);
  }

  void drawMarks() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("しおり", "Bookmark"));
    if (list_.count() == 0) {
      g.setPen(0);
      g.textCentered(TR("しおりはありません", "No bookmarks"), Gfx::W / 2, Gfx::H / 2, 26);
    } else {
      list_.draw(marksLabel, nullptr);
    }
    ui::footer(TR("A:上 B:下 B長押し:移動 A長押し:戻る", "A:Up B:Down Hold B:Go Hold A:Back"));
    g.update(Update::Fast);
  }

  void marksKey(Key k) {
    switch (k) {
      case Key::A:
        list_.up();
        drawMarks();
        break;
      case Key::B:
        list_.down();
        drawMarks();
        break;
      case Key::LongB:
        if (list_.count() > 0) {
          const Bookmark& m = gMarks.at(static_cast<uint16_t>(list_.selected()));
          BookPosition pos;
          pos.spine = m.spine;
          pos.charOffset = m.charOffset;
          gLastProgressMs = 0;
          gEngine->gotoPosition(pos);
          gEngine->saveProgress();
          enterReader(true);
        }
        break;
      case Key::LongA:
        enterMenu();
        break;
      default:
        break;
    }
  }

  // --- 位置指定 --------------------------------------------------------------

  void drawGoto() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(TR("位置を指定", "Go to position"));
    g.setPen(0);
    char line[16];
    snprintf(line, sizeof(line), "%u%%", gotoPercent_);
    g.textCentered(line, Gfx::W / 2, Gfx::H / 2 - 40, 96);
    const int16_t x = ui::kMargin * 2, w = Gfx::W - x * 2, y = Gfx::H / 2 + 20;
    g.frame(x, y, w, 24, 2);
    g.fill(x + 3, y + 3, static_cast<int16_t>((w - 6) * gotoPercent_ / 100), 18);
    ui::footer(TR("A:-5% B:+5% B長押し:移動 A長押し:戻る", "A:-5% B:+5% Hold B:Go Hold A:Back"));
    g.update(Update::Fast);
  }

  void gotoKey(Key k) {
    switch (k) {
      case Key::A:
        gotoPercent_ = gotoPercent_ >= kGotoStep ? gotoPercent_ - kGotoStep : 0;
        drawGoto();
        break;
      case Key::B:
        gotoPercent_ = gotoPercent_ + kGotoStep > 100 ? 100 : gotoPercent_ + kGotoStep;
        drawGoto();
        break;
      case Key::LongB:
        gLastProgressMs = 0;
        gEngine->gotoPermille(static_cast<uint16_t>(gotoPercent_) * 10);
        gEngine->saveProgress();
        enterReader(true);
        break;
      case Key::LongA:
        enterMenu();
        break;
      default:
        break;
    }
  }

  static constexpr uint8_t kCleanEvery = 8;

  Screen screen_ = Screen::Library;
  ui::ListView list_;
  uint8_t turnsSinceClean_ = 0;
  uint8_t gotoPercent_ = 0;
  bool settingsDirty_ = false;
};

EbookApp gApp;

}  // namespace

App& ebookApp() { return gApp; }

}  // namespace pdos
