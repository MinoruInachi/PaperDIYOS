#include "os/I18n.h"
#include "ReaderEngine.h"

#include "TitleCover.h"

#include <Arduino.h>
#include <string.h>

#include <SDCardManager.h>

#include "Config.h"
#include "PathUtil.h"
#include "Progress.h"

namespace paperdiy {
namespace {

SDCardManager& sd() { return SDCardManager::getInstance(); }

void* psAlloc(size_t bytes) {
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  return p != nullptr ? p : malloc(bytes);
}

uint32_t fnv1a(const char* s) {
  uint32_t h = 0x811C9DC5u;
  for (; *s != '\0'; ++s) {
    h ^= static_cast<uint8_t>(*s);
    h *= 16777619u;
  }
  return h;
}

bool endsWithNoCase(const char* s, const char* suffix) {
  const size_t ls = strlen(s), lt = strlen(suffix);
  return ls >= lt && strcasecmp(s + ls - lt, suffix) == 0;
}

// 章を組みながら、書き出したページ数を数えて画面に知らせる。
class CountingSink : public fb::PageSink {
 public:
  CountingSink(fb::PageSink& inner, BuildProgressFn fn, void* user)
      : inner_(inner), fn_(fn), user_(user) {}
  void onAnchor(uint32_t idHash, uint32_t charStart) override {
    inner_.onAnchor(idHash, charStart);
  }
  bool onPage(const fb::Page& page) override {
    const bool ok = inner_.onPage(page);
    ++count_;
    if (fn_ != nullptr && (count_ % 8) == 0) fn_(TR("組版中", "Typesetting"), count_, user_);
    return ok;
  }
  uint32_t count() const { return count_; }

 private:
  fb::PageSink& inner_;
  BuildProgressFn fn_;
  void* user_;
  uint32_t count_ = 0;
};

}  // namespace

bool ReaderEngine::begin(FontStore& fonts) {
  fonts_ = &fonts;
  // PSRAM の割り当て。フォントをフラッシュに置いていれば十分に余る。
  bookArena_.init(psAlloc(512u * 1024), 512u * 1024);
  indexArena_.init(psAlloc(192u * 1024), 192u * 1024);
  buildArena_.init(psAlloc(1024u * 1024), 1024u * 1024);
  renderArena_.init(psAlloc(256u * 1024), 256u * 1024);
  sd().mkdir(kAppDir, true);
  sd().mkdir(kCacheDir, true);
  sd().mkdir(kStateDir, true);
  fillLayoutParams();
  return bookArena_.capacity() > 0 && buildArena_.capacity() > 0;
}

void ReaderEngine::fillLayoutParams() {
  const uint16_t size = kFontSizes[settings_.fontSize % kFontSizeCount];
  const uint16_t gap = kLineGaps[settings_.lineGap % kLineGapCount];
  const int16_t margin = kMargins[settings_.margin % kMarginCount];

  vparams_ = jabook::VerticalLayoutParams{};
  vparams_.pageWidth = kPageWidth;
  vparams_.pageHeight = kPageHeight;
  vparams_.marginLeft = margin;
  vparams_.marginRight = margin;
  vparams_.marginTop = static_cast<int16_t>(margin + 12);   // 上下は柱のぶん広く
  vparams_.marginBottom = static_cast<int16_t>(margin + 16);
  vparams_.baseSizePx = size;
  vparams_.lineGapPct = gap;
  vparams_.ruby = settings_.ruby;
  vparams_.strictKinsoku = settings_.strictKinsoku;
  vparams_.tateChuYoko = settings_.tateChuYoko;
  vparams_.font = fonts_ != nullptr ? &fonts_->chain() : nullptr;

  hparams_ = fb::LayoutParams{};
  hparams_.pageWidth = kPageWidth;
  hparams_.pageHeight = kPageHeight;
  hparams_.marginLeft = margin;
  hparams_.marginRight = margin;
  hparams_.marginTop = static_cast<int16_t>(margin + 12);
  hparams_.marginBottom = static_cast<int16_t>(margin + 16);
  hparams_.baseSizePx = size;
  hparams_.lineSpacingPct = gap;
  hparams_.language = settings_.strictKinsoku ? "ja-strict" : "ja";
  hparams_.defaultAlign = fb::TextAlign::Justify;
  hparams_.font = fonts_ != nullptr ? &fonts_->chain() : nullptr;

  updateGeneration();
}

void ReaderEngine::updateGeneration() {
  const uint32_t print = fonts_ != nullptr ? fonts_->fingerprint() : 0;
  generation_ = vertical_ ? jabook::verticalLayoutGenerationHash(vparams_, print)
                                   : fb::layoutGenerationHash(hparams_, print);
}

void ReaderEngine::applySettings(const ReaderSettings& next) {
  const bool relayout = !(settings_ == next);
  settings_ = next;
  resolveWritingMode();
  if (relayout && open_) {
    // いま読んでいる場所を覚えて、新しい世代で開き直す。
    const BookPosition pos = position();
    chapterLoaded_ = false;
    indexArena_.reset();
    gotoPosition(pos);
  }
}

// ZIP 内のエントリの先頭 64 KB に needle があるか。CSS も XHTML もテキストなので
// そのまま部分文字列で探す。
bool ReaderEngine::entryContains(const char* href, const char* needle) {
  if (href == nullptr) return false;
  const fb::ZipEntry* entry = book_.zip().find(href);
  if (entry == nullptr) return false;

  const size_t mark = buildArena_.mark();
  fb::ZipEntryReader reader;
  bool found = false;
  if (reader.open(source_, *entry, buildArena_) == fb::BookStatus::Ok) {
    constexpr uint32_t kChunk = 1024;
    const uint32_t needleLen = static_cast<uint32_t>(strlen(needle));
    char buf[kChunk + 64];
    uint32_t carry = 0;
    uint32_t total = 0;
    while (total < 64u * 1024) {
      const int32_t got = reader.read(buf + carry, kChunk);
      if (got <= 0) break;
      const uint32_t avail = carry + static_cast<uint32_t>(got);
      buf[avail] = '\0';
      if (strstr(buf, needle) != nullptr) {
        found = true;
        break;
      }
      // チャンクの境目で切れないよう、末尾を持ち越す。
      carry = needleLen > 1 ? (needleLen - 1 < avail ? needleLen - 1 : avail) : 0;
      if (carry > 0) memmove(buf, buf + avail - carry, carry);
      total += static_cast<uint32_t>(got);
    }
  }
  buildArena_.release(mark);
  return found;
}

// 縦組みの EPUB は CSS で writing-mode: vertical-rl を指定する（そうしないと
// 縦組みで表示されない）ので、それを手がかりにする。
bool ReaderEngine::detectVerticalWriting() {
  if (isPlainText_) return true;  // .txt は日本語の縦組みを既定にする
  for (size_t i = 0; i < book_.manifestCount(); ++i) {
    const fb::ManifestItem* item = book_.manifestItem(i);
    if (item == nullptr || item->mediaType == nullptr || item->href == nullptr) continue;
    if (strstr(item->mediaType, "css") == nullptr) continue;
    if (entryContains(item->href, "vertical-rl")) return true;
  }
  // CSS を持たず、XHTML に直接書く本もある。先頭の 2 章だけ見る。
  for (uint16_t s = 0; s < spineCount_ && s < 2; ++s) {
    const fb::ManifestItem* item = book_.spineItem(s);
    if (item != nullptr && entryContains(item->href, "vertical-rl")) return true;
  }
  return false;
}

void ReaderEngine::resolveWritingMode() {
  switch (settings_.writing) {
    case WritingVertical:
      vertical_ = true;
      break;
    case WritingHorizontal:
      vertical_ = false;
      break;
    default:
      vertical_ = open_ ? detectVerticalWriting() : true;
      break;
  }
  Serial.printf("[reader] 組み方: %s（設定 %s）\n", vertical_ ? "縦組み" : "横組み",
                settings_.writing == WritingAuto      ? "自動"
                : settings_.writing == WritingVertical ? "縦組み"
                                                       : "横組み");
  fillLayoutParams();
}

bool ReaderEngine::openBook(const char* path) {
  closeBook();
  snprintf(path_, sizeof(path_), "%s", path);
  snprintf(bookId_, sizeof(bookId_), "%08lx", static_cast<unsigned long>(fnv1a(path)));

  if (!source_.open(path)) {
    Serial.printf("[reader] 開けません: %s\n", path);
    return false;
  }
  isPlainText_ = endsWithNoCase(path, ".txt");

  char dir[128];
  snprintf(dir, sizeof(dir), "%s/%s", kCacheDir, bookId_);
  if (!cache_.begin(dir)) {
    Serial.println("[reader] キャッシュディレクトリを作れません");
    source_.close();
    return false;
  }

  bookArena_.reset();
  buildArena_.reset();
  if (isPlainText_) {
    spineCount_ = 1;
    const char* slash = strrchr(path, '/');
    snprintf(title_, sizeof(title_), "%s", slash != nullptr ? slash + 1 : path);
    // 題に拡張子は要らない。
    char* dot = strrchr(title_, '.');
    if (dot != nullptr && dot != title_) *dot = '\0';
    author_[0] = '\0';
  } else {
    const fb::BookStatus st = book_.open(source_, bookArena_, buildArena_);
    if (st != fb::BookStatus::Ok) {
      Serial.printf("[reader] EPUB を開けません: %s\n", fb::bookStatusName(st));
      source_.close();
      return false;
    }
    spineCount_ = static_cast<uint16_t>(book_.spineCount());
    snprintf(title_, sizeof(title_), "%s", book_.metadata().title);
    snprintf(author_, sizeof(author_), "%s", book_.metadata().author);
    if (title_[0] == '\0') {
      const char* slash = strrchr(path, '/');
      snprintf(title_, sizeof(title_), "%s", slash != nullptr ? slash + 1 : path);
    }
  }
  buildArena_.reset();
  open_ = true;
  spine_ = 0;
  page_ = 0;
  chapterLoaded_ = false;
  buildSizeTable();      // 進捗と位置指定ジャンプの重み
  resolveWritingMode();  // 本の指定を見て縦組み／横組みを決める

  syntheticCover_ = wantsSyntheticCover();
  onCover_ = false;

  BookPosition saved;
  if (loadProgress(&saved) && saved.spine < spineCount_) {
    gotoPosition(saved);
    // 本の先頭に戻っていたなら、扉から始める。
    if (syntheticCover_ && saved.spine == 0 && saved.charOffset == 0) onCover_ = true;
  } else {
    ensureChapter(0);
    onCover_ = syntheticCover_;  // 初めて開く本は扉から
  }
  if (!hasContent()) advanceToNonEmpty(spine_);
  return true;
}

void ReaderEngine::closeBook() {
  if (open_) saveProgress();
  cache_.end();
  source_.close();
  open_ = false;
  chapterLoaded_ = false;
  spineCount_ = 0;
  spine_ = 0;
  page_ = 0;
  onCover_ = false;
  syntheticCover_ = false;
  cumulative_ = nullptr;  // bookArena_ ごと作り直されるので手放すだけでよい
  bookSize_ = 0;
  title_[0] = '\0';
  author_[0] = '\0';
}

const char* ReaderEngine::chapterHref(uint16_t spine) const {
  const fb::ManifestItem* item = book_.spineItem(spine);
  return item != nullptr ? item->href : nullptr;
}

bool ReaderEngine::buildChapter(uint16_t spine, const char* cacheName) {
  buildArena_.reset();
  fb::PageCacheWriter writer;
  if (!writer.begin(cache_, cacheName, generation_, buildArena_)) return false;

  CountingSink sink(writer, progressFn_, progressUser_);
  uint32_t pages = 0;
  uint32_t chars = 0;
  fb::BookStatus st = fb::BookStatus::Ok;

  if (isPlainText_) {
    st = vertical_ ? jabook::VerticalLayout::layoutPlainText(source_, vparams_,
                                                                      buildArena_, sink, true,
                                                                      &pages, &chars)
                            : fb::ChapterLayout::layoutPlainText(source_, hparams_, buildArena_,
                                                                 sink, &pages, &chars);
  } else {
    const char* href = chapterHref(spine);
    const fb::ZipEntry* entry = href != nullptr ? book_.zip().find(href) : nullptr;
    if (entry == nullptr) return false;
    st = vertical_
             ? jabook::VerticalLayout::layout(source_, book_.zip(), *entry, href, vparams_,
                                              buildArena_, sink, &pages, &chars)
             : fb::ChapterLayout::layout(source_, book_.zip(), *entry, href, hparams_, buildArena_,
                                         sink, &pages, &chars);
  }
  if (st != fb::BookStatus::Ok) {
    Serial.printf("[reader] 章 %u の組版に失敗: %s\n", spine, fb::bookStatusName(st));
    return false;
  }
  writer.setTotalChars(chars);
  if (!writer.finish()) return false;
  Serial.printf("[reader] 章 %u を組みました: %u ページ / %u 字 (arena %u KB)\n", spine,
                writer.pageCount(), chars, static_cast<unsigned>(buildArena_.highWater() / 1024));
  buildArena_.reset();
  return true;
}

bool ReaderEngine::ensureChapter(uint16_t spine) {
  if (chapterLoaded_ && spine_ == spine) return true;
  if (spine >= spineCount_) return false;

  char name[64];
  if (!fb::pageCacheName(spine, generation_, name, sizeof(name))) return false;

  indexArena_.reset();
  fb::BookStatus st = reader_.open(cache_, name, generation_, indexArena_);
  if (st != fb::BookStatus::Ok || reader_.isPartial()) {
    if (progressFn_ != nullptr) progressFn_(TR("組版中", "Typesetting"), 0, progressUser_);
    if (!buildChapter(spine, name)) return false;
    indexArena_.reset();
    st = reader_.open(cache_, name, generation_, indexArena_);
    if (st != fb::BookStatus::Ok) return false;
  }
  spine_ = spine;
  chapterLoaded_ = true;
  if (page_ >= reader_.pageCount()) page_ = reader_.pageCount() > 0 ? reader_.pageCount() - 1 : 0;
  return true;
}

// EPUB の先頭 spine は表紙画像だけのことが多く、縦組みは画像を置かないので
// 0 ページになる。開いた直後にそこへ止まると何も描けないので、本文のある
// 章まで送る。
bool ReaderEngine::advanceToNonEmpty(uint16_t from) {
  for (uint16_t s = from; s < spineCount_; ++s) {
    if (chapterLoaded_ && spine_ == s && reader_.pageCount() > 0) return true;
    chapterLoaded_ = false;
    page_ = 0;
    if (ensureChapter(s) && reader_.pageCount() > 0) return true;
  }
  return false;
}

// 表紙の絵も、表紙のページも持っていない本か。
//
// 市販の EPUB は spine の先頭に表紙のページ（絵 1 枚）を置いていることが
// 多く、そこに扉を足すと二重になる。表紙の絵を宣言しているかどうかを
// その目印にする。青空文庫の EPUB は何も宣言していないので扉が付く。
bool ReaderEngine::wantsSyntheticCover() const {
  if (isPlainText_) return true;  // .txt には表紙の入れようがない
  for (size_t i = 0; i < book_.manifestCount(); ++i) {
    const fb::ManifestItem* item = book_.manifestItem(i);
    if (item != nullptr && item->isCoverImage) return false;
  }
  // 青空文庫の ZIP から自分で組み直した本は、先頭に扉を持っている
  // （Aozora.cpp が OEBPS/cover.xhtml を spine の頭に置く）。足すと二重になる。
  const char* first = chapterHref(0);
  if (first != nullptr && endsWithNoCase(first, "cover.xhtml")) return false;
  return true;
}

bool ReaderEngine::renderCoverPage(uint8_t* frame) {
  if (frame == nullptr || fonts_ == nullptr) return false;
  memset(frame, 0xFF, static_cast<size_t>(kPanelWidth) * kPanelHeight);

  const size_t mark = renderArena_.mark();
  const uint16_t w = kPageWidth, h = kPageHeight;
  const uint16_t rowBytes = static_cast<uint16_t>(((w + 31) / 32) * 4);
  uint8_t* rows = renderArena_.allocArray<uint8_t>(static_cast<size_t>(rowBytes) * h);
  if (rows == nullptr) {
    renderArena_.release(mark);
    return false;
  }

  // 扉は本文（24px 前後）より大きく組みたい。紙の幅の 1/6 を上限にすると、
  // 短い題名は大きく、長い題名は自動で小さくなる。
  const bool drew = drawTitleCover(title_, author_, w, h, rowBytes, rows, fonts_->chain(),
                                   static_cast<uint16_t>(w / 6));
  if (drew) {
    // 論理 (x,y) をパネル (y, パネル高-1-x) へ。描画と同じ 90 度回転。
    for (int32_t y = 0; y < h; ++y) {
      const uint8_t* row = rows + static_cast<size_t>(y) * rowBytes;
      for (int32_t x = 0; x < w; ++x) {
        if (row[x >> 3] & (0x80u >> (x & 7))) continue;  // 1 = 白
        const int32_t px = y, py = kPanelHeight - 1 - x;
        frame[static_cast<size_t>(py) * kPanelWidth + px] = 0;
      }
    }
  }
  renderArena_.release(mark);
  return drew;
}

bool ReaderEngine::nextPage() {
  // 扉からは本文の頭へ。
  if (onCover_) {
    onCover_ = false;
    return true;
  }
  if (!open_ || !chapterLoaded_) return false;
  if (page_ + 1 < reader_.pageCount()) {
    ++page_;
    return true;
  }
  for (uint16_t s = spine_ + 1; s < spineCount_; ++s) {
    page_ = 0;
    chapterLoaded_ = false;
    if (ensureChapter(s) && reader_.pageCount() > 0) return true;
  }
  chapterLoaded_ = false;
  ensureChapter(spine_);  // 末尾で止まる
  return false;
}

bool ReaderEngine::prevPage() {
  if (onCover_) return false;  // 扉より前は無い
  if (!open_ || !chapterLoaded_) return false;
  if (page_ > 0) {
    --page_;
    return true;
  }
  // 本文の頭より前へ戻ろうとしたら扉へ。
  if (syntheticCover_ && spine_ == 0) {
    onCover_ = true;
    return true;
  }
  for (int32_t s = static_cast<int32_t>(spine_) - 1; s >= 0; --s) {
    chapterLoaded_ = false;
    page_ = 0;
    if (ensureChapter(static_cast<uint16_t>(s)) && reader_.pageCount() > 0) {
      page_ = reader_.pageCount() - 1;
      return true;
    }
  }
  chapterLoaded_ = false;
  ensureChapter(spine_);
  return false;
}

bool ReaderEngine::gotoPosition(const BookPosition& pos) {
  if (!open_) return false;
  // 本の先頭を指されたら扉へ（％ジャンプで 0% や、目次の頭を選んだとき）。
  onCover_ = syntheticCover_ && pos.spine == 0 && pos.charOffset == 0;
  chapterLoaded_ = false;
  page_ = 0;
  if (!ensureChapter(pos.spine < spineCount_ ? pos.spine : 0)) return false;
  if (reader_.pageCount() == 0) return advanceToNonEmpty(pos.spine);
  page_ = reader_.pageForChar(pos.charOffset);
  if (page_ >= reader_.pageCount()) page_ = reader_.pageCount() - 1;
  return true;
}

BookPosition ReaderEngine::position() const {
  BookPosition pos;
  pos.spine = spine_;
  pos.charOffset = chapterLoaded_ ? reader_.charStart(page_) : 0;
  return pos;
}

uint16_t ReaderEngine::tocCount() const {
  return isPlainText_ ? 0 : static_cast<uint16_t>(book_.tocCount());
}

const char* ReaderEngine::tocTitle(uint16_t index) const {
  const fb::TocEntry* entry = book_.tocEntry(index);
  return entry != nullptr ? entry->title : "";
}

bool ReaderEngine::gotoToc(uint16_t index) {
  const fb::TocEntry* entry = book_.tocEntry(index);
  if (entry == nullptr || entry->href == nullptr) return false;
  // 目次の行き先と同じ本文ファイルを spine から探す。
  for (uint16_t s = 0; s < spineCount_; ++s) {
    const char* href = chapterHref(s);
    if (href != nullptr && strcmp(href, entry->href) == 0) {
      BookPosition pos;
      pos.spine = s;
      pos.charOffset = 0;
      if (!gotoPosition(pos)) return false;
      // 章内アンカーがあればその文字位置まで送る。
      if (entry->fragment != nullptr && entry->fragment[0] != '\0') {
        uint32_t charOffset = 0;
        if (reader_.charForAnchor(fb::ZipCatalog::hashPath(entry->fragment), &charOffset)) {
          page_ = reader_.pageForChar(charOffset);
        }
      }
      return true;
    }
  }
  return false;
}

// 章ごとの展開後サイズを累積する。章の長さは本文の量にほぼ比例するので、
// これを重みにすると「短い扉のページ」と「長い本文の章」を同じ幅で数えずに済む。
// サイズが引けなかった章は 1 と数えて、表全体が 0 になるのを避ける。
void ReaderEngine::buildSizeTable() {
  cumulative_ = nullptr;
  bookSize_ = 0;
  if (spineCount_ == 0) return;

  cumulative_ = bookArena_.allocArray<uint32_t>(spineCount_);
  if (cumulative_ == nullptr) return;  // 表が無くても均等割で動く

  uint32_t running = 0;
  for (uint16_t s = 0; s < spineCount_; ++s) {
    uint32_t size = 0;
    if (isPlainText_) {
      size = static_cast<uint32_t>(source_.size());
    } else {
      const char* href = chapterHref(s);
      const fb::ZipEntry* entry = href != nullptr ? book_.zip().find(href) : nullptr;
      if (entry != nullptr) size = entry->uncompressedSize;
    }
    running += size > 0 ? size : 1;
    cumulative_[s] = running;
  }
  bookSize_ = running;
}

uint32_t ReaderEngine::cumulativeSize(uint16_t index) const {
  return cumulativeAt(cumulative_, spineCount_, index);
}

uint16_t ReaderEngine::progressPermille() const {
  if (!open_) return 0;
  if (onCover_) return 0;  // 扉は本文の手前。まだ 1 文字も読んでいない
  return paperdiy::progressPermille(cumulative_, spineCount_, spine_, page_, reader_.pageCount());
}

bool ReaderEngine::gotoPermille(uint16_t permille) {
  if (!open_) return false;

  JumpTarget target;
  if (!jumpTargetFor(cumulative_, spineCount_, permille, &target)) return false;

  BookPosition pos;
  pos.spine = target.spine;
  pos.charOffset = 0;
  if (!gotoPosition(pos)) return false;

  // 章が開けたら、章の中での比率を文字位置に直す。総文字数はキャッシュが持っている。
  if (target.span > 0 && reader_.totalChars() > 0) {
    const uint32_t charOffset = static_cast<uint32_t>(
        (static_cast<uint64_t>(reader_.totalChars()) * target.within) / target.span);
    page_ = reader_.pageForChar(charOffset);
    if (page_ >= reader_.pageCount() && reader_.pageCount() > 0) page_ = reader_.pageCount() - 1;
  }
  return true;
}

bool ReaderEngine::atEnd() const {
  if (onCover_) return false;  // 扉は本の頭
  if (!open_ || !chapterLoaded_) return false;
  if (page_ + 1 < reader_.pageCount()) return false;
  // 後ろに本文のある章が残っていないか。ここでは組み直さず、spine の数だけ見る。
  return spine_ + 1 >= spineCount_;
}

// ページ先頭のテキストを取り出す。ページは組んだ時点でキャッシュに落ちていて、
// 描画と同じ経路で読めるので、本文を組み直す必要はない。
bool ReaderEngine::pageSummary(char* out, size_t cap) const {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  if (onCover_) {  // 扉には本文が無いので、題名をその代わりに
    snprintf(out, cap, "%s", title_);
    trimUtf8(out);
    return out[0] != '\0';
  }
  if (!open_ || !chapterLoaded_) return false;

  // const のまま読みたいので、arena と reader のコピーは作らずに mark/release で戻す。
  ReaderEngine* self = const_cast<ReaderEngine*>(this);
  const size_t mark = self->renderArena_.mark();
  fb::Page page{};
  const fb::BookStatus st = self->reader_.readPage(page_, self->renderArena_, &page);
  if (st != fb::BookStatus::Ok) {
    self->renderArena_.release(mark);
    return false;
  }

  size_t used = 0;
  for (uint16_t i = 0; i < page.runCount && used + 1 < cap; ++i) {
    const fb::PageTextRun& run = page.runs[i];
    if (run.text == nullptr || run.len == 0) continue;
    for (uint16_t j = 0; j < run.len && used + 1 < cap; ++j) {
      const char c = run.text[j];
      if (c == '\n' || c == '\r' || c == '\t') continue;
      out[used++] = c;
    }
  }
  out[used] = '\0';
  self->renderArena_.release(mark);

  trimUtf8(out);  // 入りきらずに切れた文字は落とす
  return out[0] != '\0';
}

bool ReaderEngine::renderPage(uint8_t* frame) {
  if (onCover_) return renderCoverPage(frame);
  if (!open_ || !chapterLoaded_ || frame == nullptr) return false;
  memset(frame, 0xFF, static_cast<size_t>(kPanelWidth) * kPanelHeight);

  const size_t mark = renderArena_.mark();
  fb::Page page{};
  const fb::BookStatus st = reader_.readPage(page_, renderArena_, &page);
  if (st != fb::BookStatus::Ok) {
    renderArena_.release(mark);
    return false;
  }

  fb::FrameTarget target{};
  target.framebuffer = frame;
  target.width = kPanelWidth;
  target.height = kPanelHeight;
  target.widthBytes = kPanelWidth;
  target.format = fb::FrameFormat::Gray8;
  target.rotation = fb::FrameRotation::Portrait;  // 論理 540x960 を横長パネルへ

  if (vertical_) {
    jabook::VerticalRenderParams rp;
    rp.emAscentPct = vparams_.emAscentPct;
    jabook::VerticalPageRenderer::renderText(page, fonts_->chain(), target, rp);
    if (!isPlainText_) {
      // 挿絵は横組みと同じ経路（ストリーム展開 + 誤差拡散）で描く。
      fb::PageRenderer::renderImages(page, source_, book_.zip(), renderArena_, target);
    }
  } else {
    fb::PageRenderer::renderText(page, fonts_->chain(), target);
    if (!isPlainText_) {
      fb::PageRenderer::renderImages(page, source_, book_.zip(), renderArena_, target);
    }
  }
  renderArena_.release(mark);
  return true;
}

void ReaderEngine::saveProgress() const {
  if (!open_) return;
  char path[192];
  snprintf(path, sizeof(path), "%s/%s.pos", kStateDir, bookId_);
  FsFile f = sd().open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return;
  const BookPosition pos = position();
  uint32_t record[3] = {0x4A415031u /* JAP1 */, pos.spine, pos.charOffset};
  f.write(record, sizeof(record));
  f.close();
}

bool ReaderEngine::loadProgress(BookPosition* out) const {
  char path[192];
  snprintf(path, sizeof(path), "%s/%s.pos", kStateDir, bookId_);
  FsFile f = sd().open(path, O_RDONLY);
  if (!f) return false;
  uint32_t record[3] = {0, 0, 0};
  const int got = f.read(record, sizeof(record));
  f.close();
  if (got != static_cast<int>(sizeof(record)) || record[0] != 0x4A415031u) return false;
  out->spine = static_cast<uint16_t>(record[1]);
  out->charOffset = record[2];
  return true;
}

}  // namespace paperdiy
