#pragma once

// 本を開いて、組んで、ページを描くところ。
//
// 縦組み（JaBook）と横組み（FreeInkBook の ChapterLayout）を同じ器で扱う。
// どちらも同じ PageCache に落ちるので、しおり・進捗・目次ジャンプの仕組みは
// 共通。組み方を変えるとキャッシュの世代が変わり、自動で組み直される。

#include <stdint.h>

#include <FreeInkBook.h>
#include <JaVertical.h>
#include <JaVerticalRenderer.h>
#include <cache/PageCache.h>
#include <layout/ChapterLayout.h>
#include <render/PageRenderer.h>

#include "FontStore.h"
#include "SdStorage.h"

namespace paperdiy {

namespace fb = freeink::book;

// 組み方の選び方。既定は「自動」＝ 本が縦組みを指定していればそれに従う。
enum WritingMode : uint8_t {
  WritingAuto = 0,
  WritingVertical = 1,
  WritingHorizontal = 2,
};

struct ReaderSettings {
  uint8_t writing = WritingAuto;
  uint8_t fontSize = 3;      // kFontSizes の添字
  uint8_t lineGap = 2;       // kLineGaps の添字
  uint8_t margin = 1;        // kMargins の添字
  bool ruby = true;
  bool strictKinsoku = false;
  bool tateChuYoko = true;
  uint8_t autoTurn = 0;      // kAutoTurnRates の添字。0 は自動ページ送りなし
  bool showHidden = false;   // 本棚にドット始まりのものも出すか
  uint8_t frontlight = 0;    // 0..100
  uint8_t led = 2;           // 0=消灯 1=合図のみ 2=点灯
  bool invert = false;

  bool operator==(const ReaderSettings& o) const {
    return writing == o.writing && fontSize == o.fontSize && lineGap == o.lineGap &&
           margin == o.margin && ruby == o.ruby && strictKinsoku == o.strictKinsoku &&
           tateChuYoko == o.tateChuYoko;
  }
};

struct BookPosition {
  uint16_t spine = 0;
  uint32_t charOffset = 0;
};

// 章を組んでいる間の進み具合を画面に出すためのコールバック。
using BuildProgressFn = void (*)(const char* label, uint32_t pagesSoFar, void* user);

class ReaderEngine {
 public:
  bool begin(FontStore& fonts);

  bool openBook(const char* path);
  void closeBook();
  bool isOpen() const { return open_; }

  const char* title() const { return title_; }
  const char* author() const { return author_; }
  const char* path() const { return path_; }
  // キャッシュやしおりの置き場所に使う、本ごとの短い名前。
  const char* bookId() const { return bookId_; }
  uint16_t spineCount() const { return spineCount_; }

  // 目次
  uint16_t tocCount() const;
  const char* tocTitle(uint16_t index) const;
  bool gotoToc(uint16_t index);

  // ページ移動。章をまたいで進む／戻る。
  bool nextPage();
  bool prevPage();
  bool gotoPosition(const BookPosition& pos);
  BookPosition position() const;
  // 本全体の進捗（千分率）で移動する。章の長さを見て行き先を決める。
  bool gotoPermille(uint16_t permille);

  // 本の末尾（最後の章の最後のページ）にいるか。
  bool atEnd() const;

  // いま見えているページの書き出し。しおりの見出しに使う。
  // out には UTF-8 で最大 cap-1 バイト、文字の途中で切らずに詰める。
  bool pageSummary(char* out, size_t cap) const;

  // 現在ページをフレームバッファへ。frame は 8bit グレー、パネル座標系。
  bool renderPage(uint8_t* frame);

  // 表紙も扉も持たない本には、題名と著者だけの扉を 1 ページ足して、
  // 本文の前に置く。本そのものは書き換えない。
  //
  // 青空文庫が配っている EPUB には表紙が無く、開くといきなり本文が
  // 始まる。市販の本は spine の先頭に表紙のページを持っているので、
  // そういう本には足さない（二重になる）。
  bool onCoverPage() const { return onCover_; }

  // いま開いている章に描けるページがあるか。
  bool hasContent() const { return chapterLoaded_ && reader_.pageCount() > 0; }

  uint32_t chapterPage() const { return page_; }
  uint32_t chapterPages() const { return reader_.pageCount(); }
  uint16_t progressPermille() const;

  const ReaderSettings& settings() const { return settings_; }
  // いま実際に使っている組み方（自動のときは本を見て決めた結果）。
  bool isVertical() const { return vertical_; }
  // 組み方が変わっていればキャッシュ世代を作り直す（現在位置は保つ）。
  void applySettings(const ReaderSettings& next);

  void setProgressCallback(BuildProgressFn fn, void* user) {
    progressFn_ = fn;
    progressUser_ = user;
  }

  // 進捗の保存と復元（本ごと）。
  void saveProgress() const;
  bool loadProgress(BookPosition* out) const;

 private:
  bool ensureChapter(uint16_t spine);
  // 表紙だけの章など、本文が 0 ページの章を読み飛ばして次の章へ進む。
  bool advanceToNonEmpty(uint16_t from);
  // 章ごとの展開後サイズを累積した表を作る（進捗と位置指定ジャンプの土台）。
  void buildSizeTable();
  // spine 0..index の累積サイズ。表が無ければ章数での均等割にする。
  uint32_t cumulativeSize(uint16_t index) const;
  bool buildChapter(uint16_t spine, const char* cacheName);
  void updateGeneration();
  void fillLayoutParams();
  // 本が縦組みを指定しているかを見る（CSS の writing-mode: vertical-rl）。
  bool detectVerticalWriting();
  bool entryContains(const char* href, const char* needle);
  void resolveWritingMode();
  // この本に扉を足すか（表紙の絵も、表紙のページも持っていないか）。
  bool wantsSyntheticCover() const;
  bool renderCoverPage(uint8_t* frame);
  const char* chapterHref(uint16_t spine) const;

  // 合成した扉を出しているか。出しているあいだ spine_/page_ は本文の
  // 先頭（0, 0）を指したままにする。位置の保存も「本の先頭」で足りる。
  bool onCover_ = false;
  bool syntheticCover_ = false;  // この本に扉を足すと決めたか

  FontStore* fonts_ = nullptr;
  SdBookSource source_;
  SdCacheStorage cache_;
  fb::Book book_;
  fb::PageCacheReader reader_;

  fb::Arena bookArena_;
  fb::Arena indexArena_;
  fb::Arena buildArena_;
  fb::Arena renderArena_;

  jabook::VerticalLayoutParams vparams_;
  fb::LayoutParams hparams_;
  ReaderSettings settings_;

  char path_[192] = {0};
  char bookId_[16] = {0};
  char title_[128] = {0};
  char author_[96] = {0};
  bool isPlainText_ = false;
  bool vertical_ = true;  // 実際に使う組み方
  bool open_ = false;
  bool chapterLoaded_ = false;
  uint16_t spineCount_ = 0;
  uint16_t spine_ = 0;
  uint32_t page_ = 0;
  uint32_t generation_ = 0;

  // 章ごとの展開後サイズの累積（bookArena_ から取る。本を閉じるまで有効）。
  // 章の長さを測る唯一の手がかりで、これが無いと進捗は章数の均等割になる。
  uint32_t* cumulative_ = nullptr;
  uint32_t bookSize_ = 0;

  BuildProgressFn progressFn_ = nullptr;
  void* progressUser_ = nullptr;
};

}  // namespace paperdiy
