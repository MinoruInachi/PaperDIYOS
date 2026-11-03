// JaBook — 縦組みレイアウト実装
//
// 章の XHTML を SAX で流し読みしながら段落を組み、右の列から左へ流して
// ページを book::PageSink へ渡す。DOM は作らず、常駐メモリは
// 「1 段落 + 1 ページ」ぶんに固定されている（FreeInkBook と同じ約束）。

#include "JaVertical.h"

#include <new>
#include <string.h>

#include <epub/ImageProbe.h>
#include <epub/PackageParsers.h>
#include <epub/XmlSax.h>

#include "JaTextRules.h"

namespace jabook {
namespace {

using fb::BookStatus;
using fb::Page;
using fb::PageSink;
using fb::PageTextRun;

// 1 段落あたりの上限。超えたぶんは段落を分割して組む（表示は連続する）。
// 組版の挙動を変えたらこれを上げる（キャッシュが自動で作り直される）。
//   2: 挿絵を 1 枚 1 ページとして置くようにした
constexpr uint32_t kLayoutRevision = 2;

constexpr uint32_t kMaxParaCells = 3072;
// 1 ページの run 上限と、その UTF-8 テキスト置き場。
constexpr uint32_t kMaxPageRuns = 1536;
constexpr uint32_t kPageTextBytes = 8192;
// 挿絵は 1 枚 1 ページで置く（縦組みで本文に回り込ませるのは別の話）。
constexpr uint32_t kMaxPageImages = 2;
// 1 段落に載るルビの数と、ルビ文字列の置き場。
constexpr uint32_t kMaxRubyGroups = 192;
constexpr uint32_t kRubyTextBytes = 3072;

enum class CellForm : uint8_t {
  Upright,        // 正立（漢字・かな・全角記号）
  Rotated,        // 時計回り 90 度
  PunctTopRight,  // 正立・右上寄せ（、。）
  SmallKana,      // 正立・わずかに右上寄せ
  TateChuYoko,    // 正立・横に連ねて 1 字ぶんに収める
};

struct Cell {
  uint32_t cp;
  uint32_t charIndex;    // 章内の抽出テキストにおけるコードポイント位置
  int16_t advance;       // 列方向（下向き）の送り
  int16_t cellPx;        // 全角枠の一辺（＝この字の見た目の大きさ）
  uint16_t sizePx;       // フォントに渡すサイズ（stb は ascent+descent 基準）
  uint8_t style;         // freeink::book::StyleFlags
  CellForm form;
  bool clusterStart;     // ここで行を割ってよい
};

struct RubyGroup {
  uint32_t firstCell;
  uint32_t lastCell;    // 含む
  uint32_t textOffset;  // rubyText_ 内のバイト位置
  uint32_t textLen;
};

int16_t pct(int32_t value, uint32_t percent) {
  return static_cast<int16_t>(value * static_cast<int32_t>(percent) / 100);
}

// ---------------------------------------------------------------------------
// ページ組み立て
// ---------------------------------------------------------------------------
class PageBuilder {
 public:
  bool init(fb::Arena& arena, PageSink& sink) {
    sink_ = &sink;
    runs_ = arena.allocArray<PageTextRun>(kMaxPageRuns);
    text_ = arena.allocArray<char>(kPageTextBytes);
    images_ = arena.allocArray<fb::PageImage>(kMaxPageImages);
    return runs_ != nullptr && text_ != nullptr && images_ != nullptr;
  }

  // 挿絵を 1 枚置く。href はページのテキスト置き場へ写す（呼び出し側の
  // 一時バッファは onPage までもたない）。
  bool addImage(const char* href, int16_t x, int16_t y, uint16_t width, uint16_t height,
                uint32_t charIndex) {
    if (imageCount_ >= kMaxPageImages || href == nullptr) return true;
    const uint32_t len = static_cast<uint32_t>(strlen(href));
    if (textUsed_ + len + 1 > kPageTextBytes) return true;
    char* dst = text_ + textUsed_;
    memcpy(dst, href, len + 1);
    textUsed_ += len + 1;
    fb::PageImage& image = images_[imageCount_++];
    image.href = dst;
    image.x = x;
    image.y = y;
    image.width = width;
    image.height = height;
    if (!hasChar_) {
      firstChar_ = charIndex;
      hasChar_ = true;
    }
    return true;
  }

  // 1 run 追加。cps は UTF-8 に符号化して溜める。
  bool addRun(int16_t x, int16_t y, uint16_t sizePx, uint8_t style, const uint32_t* cps,
              uint32_t count, uint32_t charIndex) {
    if (count == 0) return true;
    if (runCount_ >= kMaxPageRuns) return true;  // 落とすが組版は続ける
    if (textUsed_ + count * 4 + 1 > kPageTextBytes) return true;
    char* dst = text_ + textUsed_;
    uint32_t len = 0;
    for (uint32_t i = 0; i < count; ++i) len += utf8Encode(cps[i], dst + len);
    dst[len] = '\0';
    textUsed_ += len + 1;

    PageTextRun& run = runs_[runCount_++];
    run.text = dst;
    run.len = static_cast<uint16_t>(len);
    run.x = x;
    run.baselineY = y;
    run.sizePx = sizePx;
    run.styleFlags = style;
    if (!hasChar_) {
      firstChar_ = charIndex;
      hasChar_ = true;
    }
    return true;
  }

  bool empty() const { return runCount_ == 0 && imageCount_ == 0; }

  // 溜めた run を 1 ページとして sink へ渡し、バッファを空にする。
  bool flush() {
    if (runCount_ == 0 && imageCount_ == 0) return true;
    Page page{};
    page.runs = runs_;
    page.runCount = runCount_;
    page.images = images_;
    page.imageCount = imageCount_;
    page.links = nullptr;
    page.linkCount = 0;
    page.pageIndex = pageIndex_;
    page.charStart = firstChar_;
    const bool keepGoing = sink_->onPage(page);
    ++pageIndex_;
    runCount_ = 0;
    imageCount_ = 0;
    textUsed_ = 0;
    hasChar_ = false;
    firstChar_ = 0;
    return keepGoing;
  }

  uint32_t pageCount() const { return pageIndex_; }

 private:
  PageSink* sink_ = nullptr;
  PageTextRun* runs_ = nullptr;
  fb::PageImage* images_ = nullptr;
  char* text_ = nullptr;
  uint32_t textUsed_ = 0;
  uint16_t runCount_ = 0;
  uint16_t imageCount_ = 0;
  uint32_t pageIndex_ = 0;
  uint32_t firstChar_ = 0;
  bool hasChar_ = false;
};

// ---------------------------------------------------------------------------
// 縦組みエンジン本体
// ---------------------------------------------------------------------------
class VerticalEngine {
 public:
  // source/zip/chapterHref は挿絵の解決に使う（プレーンテキストでは null）。
  bool init(const VerticalLayoutParams& params, fb::Arena& arena, PageSink& sink,
            fb::BookSource* source = nullptr, const fb::ZipCatalog* zip = nullptr,
            const char* chapterHref = nullptr) {
    params_ = params;
    arena_ = &arena;
    source_ = source;
    zip_ = zip;
    if (!fb::dirName(chapterHref != nullptr ? chapterHref : "", chapterDir_,
                     sizeof(chapterDir_))) {
      chapterDir_[0] = '\0';
    }
    font_ = params.font;
    if (font_ == nullptr) return false;
    if (!pages_.init(arena, sink)) return false;
    cells_ = arena.allocArray<Cell>(kMaxParaCells);
    ruby_ = arena.allocArray<RubyGroup>(kMaxRubyGroups);
    rubyText_ = arena.allocArray<char>(kRubyTextBytes);
    scratchCps_ = arena.allocArray<uint32_t>(64);
    if (cells_ == nullptr || ruby_ == nullptr || rubyText_ == nullptr || scratchCps_ == nullptr) {
      return false;
    }

    base_ = static_cast<int16_t>(params.baseSizePx);
    bodyFontSize_ = fontSizeForEm(base_);
    blockEm_ = base_;
    blockSize_ = bodyFontSize_;
    lineAdvance_ = pct(base_, params.lineGapPct);
    if (lineAdvance_ < base_ + 1) lineAdvance_ = static_cast<int16_t>(base_ + 1);
    rubySize_ = params.ruby ? pct(base_, params.rubySizePct) : 0;
    rubyFontSize_ = rubySize_ > 0 ? fontSizeForEm(rubySize_) : 0;
    topY_ = params.marginTop;
    colHeight_ = static_cast<int16_t>(params.pageHeight - params.marginTop - params.marginBottom);
    rightEdge_ = static_cast<int16_t>(params.pageWidth - params.marginRight - rubySize_);
    const int16_t span = static_cast<int16_t>(rightEdge_ - base_ - params.marginLeft);
    columnsPerPage_ = span >= 0 ? static_cast<int16_t>(span / lineAdvance_ + 1) : 1;
    if (columnsPerPage_ < 1) columnsPerPage_ = 1;
    return colHeight_ > base_;
  }

  // --- 段落の組み立て -------------------------------------------------------

  void resetParagraph() {
    cellCount_ = 0;
    rubyCount_ = 0;
    rubyTextUsed_ = 0;
    pendingSpace_ = false;
    paraStarted_ = false;
  }

  void setBlockStyle(uint16_t emPx, bool heading) {
    blockEm_ = static_cast<int16_t>(emPx);
    blockSize_ = fontSizeForEm(blockEm_);
    heading_ = heading;
  }

  void pushCodepoint(uint32_t cp, uint8_t style) {
    if (cellCount_ >= kMaxParaCells) {
      // 段落が長すぎる。ここまでを組んで続きを新しい段落として扱う。
      layoutParagraph(/*continuation=*/true);
      cellCount_ = 0;
      rubyCount_ = 0;
      rubyTextUsed_ = 0;
    }
    Cell& c = cells_[cellCount_];
    c.cp = cp;
    c.charIndex = charCount_;
    c.cellPx = blockEm_;
    c.sizePx = blockSize_;
    c.style = style;
    c.clusterStart = true;
    const VertForm vf = vertFormOf(cp);
    switch (vf) {
      case VertForm::PunctTopRight:
        c.form = CellForm::PunctTopRight;
        break;
      case VertForm::SmallKana:
        c.form = CellForm::SmallKana;
        break;
      case VertForm::Rotated:
        c.form = CellForm::Rotated;
        break;
      case VertForm::Upright:
      default:
        // 全角でない未分類の字（キリル・ハングル等）は倒して組む。
        c.form = (cp < 0x80 || !isFullWidth(cp)) ? CellForm::Rotated : CellForm::Upright;
        break;
    }
    c.advance = advanceFor(c);
    ++cellCount_;
    ++charCount_;
  }

  // XHTML のテキストノード。空白は 1 個に畳む（和文の改行は空白にしない）。
  void addText(const char* text, uint32_t len, uint8_t style) {
    uint32_t i = 0;
    while (i < len) {
      const uint32_t cp = utf8Next(text, len, &i);
      if (cp == 0) break;
      if (isSpace(cp) && cp != 0x3000) {
        if (paraStarted_) pendingSpace_ = true;
        continue;
      }
      if (pendingSpace_) {
        pendingSpace_ = false;
        // 和文どうしの改行では空白を入れない（EPUB の整形改行対策）。
        const uint32_t prev = cellCount_ > 0 ? cells_[cellCount_ - 1].cp : 0;
        if (!(isFullWidth(prev) && isFullWidth(cp))) {
          if (cellCount_ > 0) pushCodepoint(' ', style);
        }
      }
      if (rubyDepth_ > 0 && inRubyText_) {
        appendRubyText(cp);
      } else {
        paraStarted_ = true;
        pushCodepoint(cp, style);
      }
    }
  }

  // --- ルビ -----------------------------------------------------------------

  void beginRuby() {
    ++rubyDepth_;
    rubyBaseFirst_ = cellCount_;
    rubyPendingOffset_ = rubyTextUsed_;
    inRubyText_ = false;
  }
  void beginRubyText() { inRubyText_ = true; }
  void endRubyText() { inRubyText_ = false; }
  void endRuby() {
    if (rubyDepth_ == 0) return;
    --rubyDepth_;
    inRubyText_ = false;
    const uint32_t len = rubyTextUsed_ - rubyPendingOffset_;
    if (len > 0 && cellCount_ > rubyBaseFirst_ && rubyCount_ < kMaxRubyGroups &&
        params_.ruby) {
      RubyGroup& g = ruby_[rubyCount_++];
      g.firstCell = rubyBaseFirst_;
      g.lastCell = cellCount_ - 1;
      g.textOffset = rubyPendingOffset_;
      g.textLen = len;
      // ルビ付きの親文字列は途中で割らない。
      for (uint32_t i = rubyBaseFirst_ + 1; i < cellCount_; ++i) cells_[i].clusterStart = false;
    } else {
      rubyTextUsed_ = rubyPendingOffset_;
    }
  }

  // 青空文庫形式のルビを、テキスト入力から直接組む。
  void addAozoraText(const char* text, uint32_t len, uint8_t style);

  // <img> を 1 枚 1 ページの挿絵として置く。縦組みで本文に回り込ませるのは
  // 別の話なので、まずは版面いっぱいに中央配置する（拡大はしない）。
  void addImage(const char* src) {
    if (stopped_ || src == nullptr || arena_ == nullptr || source_ == nullptr ||
        zip_ == nullptr) {
      return;
    }
    const size_t mark = arena_->mark();
    const char* path = fb::resolveHref(*arena_, chapterDir_, src, nullptr);
    if (path == nullptr) {
      arena_->release(mark);
      return;
    }
    const fb::ZipEntry* entry = zip_->findByHash(fb::ZipCatalog::hashPath(path));
    if (entry == nullptr) {
      arena_->release(mark);
      return;
    }
    fb::ImageInfo info;
    const fb::BookStatus st = fb::probeImage(*source_, *entry, *arena_, &info);
    if (st != fb::BookStatus::Ok || info.kind == fb::ImageInfo::Kind::Unknown ||
        info.width == 0 || info.height == 0) {
      arena_->release(mark);  // GIF/SVG など読めない形式は黙って飛ばす
      return;
    }

    // ここまでの本文を出しきってから、挿絵だけのページを 1 枚出す。
    if (!layoutParagraph(false)) {
      arena_->release(mark);
      return;
    }
    cellCount_ = 0;
    rubyCount_ = 0;
    rubyTextUsed_ = 0;
    if (!pages_.flush()) {
      stopped_ = true;
      arena_->release(mark);
      return;
    }
    columnIndex_ = 0;

    const int32_t availW = params_.pageWidth - params_.marginLeft - params_.marginRight;
    const int32_t availH = params_.pageHeight - params_.marginTop - params_.marginBottom;
    int32_t w = info.width;
    int32_t h = info.height;
    if (w > availW) {
      h = h * availW / w;
      w = availW;
    }
    if (h > availH) {
      w = w * availH / h;
      h = availH;
    }
    if (w > 0 && h > 0) {
      pages_.addImage(path, static_cast<int16_t>((params_.pageWidth - w) / 2),
                      static_cast<int16_t>((params_.pageHeight - h) / 2),
                      static_cast<uint16_t>(w), static_cast<uint16_t>(h), charCount_);
      if (!pages_.flush()) stopped_ = true;
      columnIndex_ = 0;
    }
    arena_->release(mark);
  }

  // --- 段落の確定 -----------------------------------------------------------

  bool endParagraph() {
    const bool ok = layoutParagraph(false);
    resetParagraph();
    if (heading_ && params_.headingSpaceTenths > 0) skipColumns(1);
    return ok;
  }

  // 改行 <br/>：段落を確定せずに列だけ改める。
  bool breakLine() {
    const bool ok = layoutParagraph(false);
    cellCount_ = 0;
    rubyCount_ = 0;
    rubyTextUsed_ = 0;
    pendingSpace_ = false;
    return ok;
  }

  bool finish() {
    if (!endParagraph()) return false;
    return pages_.flush();
  }

  uint32_t pageCount() const { return pages_.pageCount(); }
  uint32_t totalChars() const { return charCount_; }
  bool stopped() const { return stopped_; }

 private:
  int16_t advanceFor(const Cell& c) const {
    if (c.form == CellForm::Rotated) return font_->advance(c.cp, c.sizePx, c.style);
    return c.cellPx;
  }

  // stb_truetype のサイズ指定は ascent+descent 基準なので、和文の全角が
  // 指定どおりの実寸になるフォントサイズを逆算する。
  uint16_t fontSizeForEm(int16_t emPx) const {
    uint16_t s = static_cast<uint16_t>(emPx > 0 ? emPx : 1);
    for (int i = 0; i < 4; ++i) {
      const int16_t adv = font_->advance(0x4E00, s, 0);  // 「一」＝全角
      if (adv <= 0) return static_cast<uint16_t>(emPx);
      const int32_t next = (static_cast<int32_t>(emPx) * s + adv / 2) / adv;
      if (next <= 0) return s;
      if (next == static_cast<int32_t>(s)) break;
      s = static_cast<uint16_t>(next);
    }
    return s;
  }

  void appendRubyText(uint32_t cp) {
    char buf[4];
    const uint32_t n = utf8Encode(cp, buf);
    if (rubyTextUsed_ + n >= kRubyTextBytes) return;
    memcpy(rubyText_ + rubyTextUsed_, buf, n);
    rubyTextUsed_ += n;
  }

  // 縦中横と欧文まとまりの決定。段落が確定してから一度だけ通す。
  void postProcess() {
    for (uint32_t i = 0; i < cellCount_;) {
      if (cells_[i].form != CellForm::Rotated) {
        ++i;
        continue;
      }
      // 半角数字の連なりを数える。
      if (params_.tateChuYoko && isAsciiDigit(cells_[i].cp)) {
        uint32_t j = i;
        while (j < cellCount_ && isAsciiDigit(cells_[j].cp)) ++j;
        const uint32_t digits = j - i;
        const bool boundedLeft = i == 0 || !isLatinRun(cells_[i - 1].cp);
        const bool boundedRight = j >= cellCount_ || !isLatinRun(cells_[j].cp);
        if (digits <= 2 && boundedLeft && boundedRight) {
          cells_[i].form = CellForm::TateChuYoko;
          cells_[i].advance = cells_[i].cellPx;
          for (uint32_t k = i + 1; k < j; ++k) {
            cells_[k].form = CellForm::TateChuYoko;
            cells_[k].advance = 0;
            cells_[k].clusterStart = false;
          }
          i = j;
          continue;
        }
        i = j;
        continue;
      }
      // 欧文の語は途中で割らない。
      if (isLatinRun(cells_[i].cp) && cells_[i].cp != ' ') {
        uint32_t j = i + 1;
        while (j < cellCount_ && cells_[j].form == CellForm::Rotated &&
               isLatinRun(cells_[j].cp) && cells_[j].cp != ' ') {
          cells_[j].clusterStart = false;
          ++j;
        }
        i = j;
        continue;
      }
      ++i;
    }
  }

  uint32_t clusterStartAtOrBefore(uint32_t index, uint32_t floorIndex) const {
    uint32_t j = index;
    while (j > floorIndex && !cells_[j].clusterStart) --j;
    return j;
  }

  // 列の切れ目を禁則にあわせて手前へずらす。
  uint32_t applyKinsoku(uint32_t start, uint32_t end) const {
    for (int guard = 0; guard < 4; ++guard) {
      bool changed = false;
      if (end < cellCount_ && isLineStartProhibited(cells_[end].cp, params_.strictKinsoku)) {
        const uint32_t cand = clusterStartAtOrBefore(end - 1, start);
        if (cand > start && cand < end) {
          end = cand;
          changed = true;
        }
      }
      if (end > start && isLineEndProhibited(cells_[end - 1].cp)) {
        const uint32_t cand = clusterStartAtOrBefore(end - 1, start);
        if (cand > start && cand < end) {
          end = cand;
          changed = true;
        }
      }
      if (!changed) break;
    }
    return end;
  }

  bool layoutParagraph(bool continuation) {
    (void)continuation;
    if (cellCount_ == 0 || stopped_) return !stopped_;
    postProcess();

    uint32_t pos = 0;
    bool firstLine = true;
    while (pos < cellCount_ && !stopped_) {
      int16_t offset = 0;
      if (firstLine && !continuation && params_.firstLineIndentTenths > 0 &&
          cells_[pos].cp != 0x3000) {
        offset = static_cast<int16_t>(base_ * params_.firstLineIndentTenths / 10);
      }
      const int16_t avail = static_cast<int16_t>(colHeight_ - offset);

      uint32_t end = pos;
      int16_t used = 0;
      bool hung = false;
      while (end < cellCount_) {
        const int16_t adv = cells_[end].advance;
        if (used + adv > avail) {
          if (params_.hangingPunctuation && end > pos &&
              cells_[end].form == CellForm::PunctTopRight) {
            ++end;  // 版面の外へぶら下げる
            hung = true;
          }
          break;
        }
        used = static_cast<int16_t>(used + adv);
        ++end;
      }
      if (end <= pos) end = pos + 1;  // 1 文字も入らない異常時の前進保証
      if (end < cellCount_ && !hung) {
        while (end > pos && !cells_[end].clusterStart) --end;
        end = applyKinsoku(pos, end);
        if (end <= pos) end = pos + 1;
      }
      if (!emitColumn(pos, end, offset)) return false;
      pos = end;
      firstLine = false;
    }
    return !stopped_;
  }

  int16_t columnLeft(int16_t index) const {
    return static_cast<int16_t>(rightEdge_ - base_ - index * lineAdvance_);
  }

  bool ensureColumn() {
    if (columnIndex_ >= columnsPerPage_) {
      if (!pages_.flush()) {
        stopped_ = true;
        return false;
      }
      columnIndex_ = 0;
    }
    return true;
  }

  void skipColumns(int16_t count) {
    for (int16_t i = 0; i < count; ++i) {
      if (columnIndex_ < columnsPerPage_) ++columnIndex_;
    }
  }

  bool emitColumn(uint32_t from, uint32_t to, int16_t offset) {
    if (!ensureColumn()) return false;
    const int16_t left = columnLeft(columnIndex_);
    int16_t y = static_cast<int16_t>(topY_ + offset);

    uint32_t i = from;
    while (i < to) {
      const Cell& c = cells_[i];
      const int16_t cellW = c.cellPx;
      const int16_t ascent = pct(cellW, params_.emAscentPct);
      switch (c.form) {
        case CellForm::Rotated: {
          // 続く回転セルをひとつの run にまとめる（描画側が advance で進む）。
          uint32_t j = i;
          uint32_t n = 0;
          int16_t total = 0;
          while (j < to && cells_[j].form == CellForm::Rotated && cells_[j].style == c.style &&
                 cells_[j].sizePx == c.sizePx && n < 64) {
            scratchCps_[n++] = cells_[j].cp;
            total = static_cast<int16_t>(total + cells_[j].advance);
            ++j;
          }
          const int16_t x = static_cast<int16_t>(left);
          pages_.addRun(x, y, c.sizePx, static_cast<uint8_t>(c.style | VertRotated), scratchCps_,
                        n, c.charIndex);
          y = static_cast<int16_t>(y + total);
          i = j;
          break;
        }
        case CellForm::TateChuYoko: {
          uint32_t j = i;
          uint32_t n = 0;
          int16_t width = 0;
          while (j < to && cells_[j].form == CellForm::TateChuYoko && n < 8 &&
                 (j == i || !cells_[j].clusterStart)) {
            scratchCps_[n++] = cells_[j].cp;
            width = static_cast<int16_t>(width + font_->advance(cells_[j].cp, cells_[j].sizePx,
                                                                cells_[j].style));
            ++j;
          }
          const int16_t x = static_cast<int16_t>(left + (cellW - width) / 2);
          pages_.addRun(x, static_cast<int16_t>(y + ascent), c.sizePx, c.style, scratchCps_, n,
                        c.charIndex);
          y = static_cast<int16_t>(y + cellW);
          i = j;
          break;
        }
        default: {
          const int16_t glyphW = font_->advance(c.cp, c.sizePx, c.style);
          int16_t x = static_cast<int16_t>(left + (cellW - glyphW) / 2);
          int16_t baseline = static_cast<int16_t>(y + ascent);
          if (c.form == CellForm::PunctTopRight) {
            x = static_cast<int16_t>(x + cellW / 2);
            baseline = static_cast<int16_t>(baseline - cellW / 2);
          } else if (c.form == CellForm::SmallKana && params_.smallKanaShiftPct > 0) {
            const int16_t d = pct(cellW, params_.smallKanaShiftPct);
            x = static_cast<int16_t>(x + d);
            baseline = static_cast<int16_t>(baseline - d);
          }
          const uint32_t cp = c.cp;
          pages_.addRun(x, baseline, c.sizePx, c.style, &cp, 1, c.charIndex);
          y = static_cast<int16_t>(y + c.advance);
          ++i;
          break;
        }
      }
    }

    emitRuby(from, to, left, offset);
    ++columnIndex_;
    return true;
  }

  // この列に収まったルビを、本文列の右脇へ組む。
  void emitRuby(uint32_t from, uint32_t to, int16_t left, int16_t offset) {
    if (!params_.ruby || rubySize_ <= 0) return;
    for (uint32_t g = 0; g < rubyCount_; ++g) {
      const RubyGroup& grp = ruby_[g];
      if (grp.firstCell < from || grp.lastCell >= to) continue;
      // 親文字列の列方向の範囲
      int16_t y = static_cast<int16_t>(topY_ + offset);
      for (uint32_t i = from; i < grp.firstCell; ++i) y = static_cast<int16_t>(y + cells_[i].advance);
      int16_t span = 0;
      for (uint32_t i = grp.firstCell; i <= grp.lastCell; ++i) {
        span = static_cast<int16_t>(span + cells_[i].advance);
      }

      // ルビ文字を取り出す
      uint32_t count = 0;
      uint32_t bi = 0;
      const char* rt = rubyText_ + grp.textOffset;
      while (bi < grp.textLen && count < 64) {
        const uint32_t cp = utf8Next(rt, grp.textLen, &bi);
        if (cp == 0) break;
        scratchCps_[count++] = cp;
      }
      if (count == 0) continue;

      int16_t adv = rubySize_;
      if (static_cast<int32_t>(adv) * static_cast<int32_t>(count) > static_cast<int32_t>(span)) {
        adv = static_cast<int16_t>(span / static_cast<int32_t>(count));
        if (adv <= 0) continue;
      }
      const int16_t used = static_cast<int16_t>(adv * static_cast<int32_t>(count));
      int16_t ry = static_cast<int16_t>(y + (span - used) / 2);
      const int16_t rx = static_cast<int16_t>(left + base_);
      const int16_t rAscent = pct(rubySize_, params_.emAscentPct);
      for (uint32_t k = 0; k < count; ++k) {
        const int16_t glyphW = font_->advance(scratchCps_[k], rubyFontSize_, 0);
        const int16_t x = static_cast<int16_t>(rx + (rubySize_ - glyphW) / 2);
        pages_.addRun(x, static_cast<int16_t>(ry + rAscent), rubyFontSize_, VertRuby,
                      &scratchCps_[k], 1, cells_[grp.firstCell].charIndex);
        ry = static_cast<int16_t>(ry + adv);
      }
    }
  }

 private:
  VerticalLayoutParams params_;
  fb::BookFont* font_ = nullptr;
  PageBuilder pages_;
  fb::Arena* arena_ = nullptr;
  fb::BookSource* source_ = nullptr;
  const fb::ZipCatalog* zip_ = nullptr;
  char chapterDir_[256] = {0};

  Cell* cells_ = nullptr;
  uint32_t cellCount_ = 0;
  RubyGroup* ruby_ = nullptr;
  uint32_t rubyCount_ = 0;
  char* rubyText_ = nullptr;
  uint32_t rubyTextUsed_ = 0;
  uint32_t* scratchCps_ = nullptr;

  uint32_t rubyDepth_ = 0;
  uint32_t rubyBaseFirst_ = 0;
  uint32_t rubyPendingOffset_ = 0;
  bool inRubyText_ = false;

  int16_t blockEm_ = 24;
  uint16_t blockSize_ = 24;
  uint16_t bodyFontSize_ = 24;
  uint16_t rubyFontSize_ = 12;
  bool heading_ = false;
  bool pendingSpace_ = false;
  bool paraStarted_ = false;
  bool stopped_ = false;

  // 青空文庫形式のテキストを読むときの状態
  uint32_t aozoraBaseMark_ = UINT32_MAX;
  uint32_t aozoraRubyStart_ = 0;
  uint32_t aozoraNoteDepth_ = 0;
  bool aozoraRubyOpen_ = false;
  bool lastWasNewline_ = false;

  uint32_t charCount_ = 0;

  int16_t base_ = 24;
  int16_t lineAdvance_ = 42;
  int16_t rubySize_ = 12;
  int16_t topY_ = 40;
  int16_t colHeight_ = 720;
  int16_t rightEdge_ = 452;
  int16_t columnsPerPage_ = 10;
  int16_t columnIndex_ = 0;
};

// ---------------------------------------------------------------------------
// 青空文庫形式のルビ（｜親文字《るび》 / 漢字《かんじ》）と注記［＃…］
// ---------------------------------------------------------------------------
void VerticalEngine::addAozoraText(const char* text, uint32_t len, uint8_t style) {
  uint32_t i = 0;
  while (i < len) {
    const uint32_t cp = utf8Next(text, len, &i);
    if (cp == 0) break;
    switch (cp) {
      case 0xFF5C:  // ｜ ルビの親文字はここから
        aozoraBaseMark_ = cellCount_;
        continue;
      case 0x300A: {  // 《 ルビ本体
        aozoraRubyOpen_ = true;
        aozoraRubyStart_ = rubyTextUsed_;
        if (aozoraBaseMark_ == UINT32_MAX) {
          // 直前の同種文字列（漢字・カタカナ）をさかのぼって親文字とする
          uint32_t j = cellCount_;
          while (j > 0) {
            const uint32_t p = cells_[j - 1].cp;
            const bool kanji = (p >= 0x4E00 && p <= 0x9FFF) || (p >= 0x3005 && p <= 0x3007);
            const bool kana = p >= 0x30A0 && p <= 0x30FF;
            if (!kanji && !kana) break;
            --j;
          }
          aozoraBaseMark_ = j;
        }
        continue;
      }
      case 0x300B: {  // 》
        if (aozoraRubyOpen_) {
          aozoraRubyOpen_ = false;
          const uint32_t rtLen = rubyTextUsed_ - aozoraRubyStart_;
          if (rtLen > 0 && aozoraBaseMark_ != UINT32_MAX && cellCount_ > aozoraBaseMark_ &&
              rubyCount_ < kMaxRubyGroups && params_.ruby) {
            RubyGroup& g = ruby_[rubyCount_++];
            g.firstCell = aozoraBaseMark_;
            g.lastCell = cellCount_ - 1;
            g.textOffset = aozoraRubyStart_;
            g.textLen = rtLen;
            for (uint32_t k = aozoraBaseMark_ + 1; k < cellCount_; ++k) {
              cells_[k].clusterStart = false;
            }
          } else {
            rubyTextUsed_ = aozoraRubyStart_;
          }
          aozoraBaseMark_ = UINT32_MAX;
        }
        continue;
      }
      case 0xFF3B:  // ［＃ 注記の始まり
        ++aozoraNoteDepth_;
        continue;
      case 0xFF3D:  // ］
        if (aozoraNoteDepth_ > 0) --aozoraNoteDepth_;
        continue;
      default:
        break;
    }
    if (aozoraNoteDepth_ > 0) continue;
    if (aozoraRubyOpen_) {
      appendRubyText(cp);
      continue;
    }
    if (cp == '\n') {
      // 空行は段落の区切り、単独改行は改行として扱う。
      if (lastWasNewline_) {
        endParagraph();
        lastWasNewline_ = false;
      } else {
        lastWasNewline_ = true;
      }
      continue;
    }
    if (cp == '\r') continue;
    if (lastWasNewline_) {
      lastWasNewline_ = false;
      if (cellCount_ > 0) breakLine();
    }
    paraStarted_ = true;
    pushCodepoint(cp, style);
  }
}

// ---------------------------------------------------------------------------
// XHTML → 縦組み
// ---------------------------------------------------------------------------
const char* localName(const char* qname) {
  const char* colon = strrchr(qname, ':');
  return colon != nullptr ? colon + 1 : qname;
}

bool eq(const char* a, const char* b) { return strcmp(a, b) == 0; }

bool isSkipped(const char* local) {
  return eq(local, "head") || eq(local, "style") || eq(local, "script") || eq(local, "title") ||
         eq(local, "rp");
}

bool isBlock(const char* local) {
  return eq(local, "p") || eq(local, "div") || eq(local, "li") || eq(local, "blockquote") ||
         eq(local, "section") || eq(local, "article") || eq(local, "pre") || eq(local, "tr") ||
         eq(local, "dd") || eq(local, "dt") || eq(local, "figcaption") || eq(local, "td");
}

int headingLevel(const char* local) {
  if (local[0] == 'h' && local[1] >= '1' && local[1] <= '6' && local[2] == '\0') {
    return local[1] - '0';
  }
  return 0;
}

const char* attrLocal(const char** atts, const char* want) {
  if (atts == nullptr) return nullptr;
  for (uint32_t i = 0; atts[i] != nullptr && atts[i + 1] != nullptr; i += 2) {
    if (eq(localName(atts[i]), want)) return atts[i + 1];
  }
  return nullptr;
}

class ChapterHandler : public fb::XmlHandler {
 public:
  ChapterHandler(VerticalEngine& engine, const VerticalLayoutParams& params, PageSink& sink)
      : engine_(engine), params_(params), sink_(sink) {
    engine_.setBlockStyle(params.baseSizePx, false);
  }

  void onStartElement(const char* name, const char** atts) override {
    const char* local = localName(name);
    if (isSkipped(local)) {
      ++skipDepth_;
      return;
    }
    if (skipDepth_ > 0) return;

    const char* id = attrLocal(atts, "id");
    if (id != nullptr && id[0] != '\0') {
      sink_.onAnchor(fb::ZipCatalog::hashPath(id), engine_.totalChars());
    }

    if (eq(local, "br")) {
      engine_.breakLine();
      return;
    }
    if (eq(local, "img") || eq(local, "image")) {
      const char* src = attrLocal(atts, "src");
      if (src == nullptr) src = attrLocal(atts, "href");  // svg:image
      if (src != nullptr && src[0] != '\0') engine_.addImage(src);
      return;
    }
    if (eq(local, "ruby")) {
      engine_.beginRuby();
      return;
    }
    if (eq(local, "rt")) {
      engine_.beginRubyText();
      return;
    }
    if (eq(local, "b") || eq(local, "strong")) {
      ++bold_;
      return;
    }
    if (eq(local, "i") || eq(local, "em") || eq(local, "cite")) {
      ++italic_;
      return;
    }
    const int h = headingLevel(local);
    if (h > 0) {
      engine_.endParagraph();
      uint32_t size = static_cast<uint32_t>(params_.baseSizePx) * params_.headingScalePct / 100;
      if (h >= 4) size = params_.baseSizePx;
      engine_.setBlockStyle(static_cast<uint16_t>(size), true);
      ++bold_;
      return;
    }
    if (isBlock(local)) {
      engine_.endParagraph();
      engine_.setBlockStyle(params_.baseSizePx, false);
      return;
    }
  }

  void onEndElement(const char* name) override {
    const char* local = localName(name);
    if (isSkipped(local)) {
      if (skipDepth_ > 0) --skipDepth_;
      return;
    }
    if (skipDepth_ > 0) return;

    if (eq(local, "ruby")) {
      engine_.endRuby();
      return;
    }
    if (eq(local, "rt")) {
      engine_.endRubyText();
      return;
    }
    if (eq(local, "b") || eq(local, "strong")) {
      if (bold_ > 0) --bold_;
      return;
    }
    if (eq(local, "i") || eq(local, "em") || eq(local, "cite")) {
      if (italic_ > 0) --italic_;
      return;
    }
    const int h = headingLevel(local);
    if (h > 0) {
      engine_.endParagraph();
      engine_.setBlockStyle(params_.baseSizePx, false);
      if (bold_ > 0) --bold_;
      return;
    }
    if (isBlock(local)) {
      engine_.endParagraph();
      engine_.setBlockStyle(params_.baseSizePx, false);
      return;
    }
  }

  void onText(const char* text, int len) override {
    if (skipDepth_ > 0 || len <= 0) return;
    engine_.addText(text, static_cast<uint32_t>(len), style());
    if (engine_.stopped()) stopParse = true;
  }

 private:
  uint8_t style() const {
    uint8_t s = 0;
    if (bold_ > 0) s |= freeink::font::StyleBold;
    if (italic_ > 0) s |= freeink::font::StyleItalic;
    return s;
  }

  VerticalEngine& engine_;
  const VerticalLayoutParams& params_;
  PageSink& sink_;
  uint32_t skipDepth_ = 0;
  uint32_t bold_ = 0;
  uint32_t italic_ = 0;
};

// 進捗の分母となる文字数を数えるだけの軽い BookSource ラッパは持たず、
// プレーンテキストはチャンク読みしてそのまま流す。
constexpr uint32_t kTextChunk = 2048;

}  // namespace

uint32_t verticalLayoutGenerationHash(const VerticalLayoutParams& p, uint32_t fontFingerprint) {
  // FNV-1a。縦組み固有の定数を混ぜて横組みのハッシュと衝突しないようにする。
  uint32_t h = 0x811C9DC5u ^ 0x56455254u;  // 'VERT'
  auto mix = [&h](uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      h ^= (v >> (i * 8)) & 0xFFu;
      h *= 16777619u;
    }
  };
  mix(static_cast<uint32_t>(p.pageWidth));
  mix(static_cast<uint32_t>(p.pageHeight));
  mix(static_cast<uint32_t>(p.marginTop));
  mix(static_cast<uint32_t>(p.marginBottom));
  mix(static_cast<uint32_t>(p.marginLeft));
  mix(static_cast<uint32_t>(p.marginRight));
  mix(p.baseSizePx);
  mix(p.lineGapPct);
  mix(p.paragraphSpacingPct);
  mix(p.firstLineIndentTenths);
  mix(p.headingScalePct);
  mix(p.headingSpaceTenths);
  mix(p.emAscentPct);
  mix(p.rubySizePct);
  mix(p.smallKanaShiftPct);
  mix((p.ruby ? 1u : 0u) | (p.tateChuYoko ? 2u : 0u) | (p.strictKinsoku ? 4u : 0u) |
      (p.hangingPunctuation ? 8u : 0u));
  mix(fontFingerprint);
  mix(kLayoutRevision);
  return h;
}

BookStatus VerticalLayout::layout(fb::BookSource& source, const fb::ZipCatalog& zip,
                                  const fb::ZipEntry& entry, const char* chapterHref,
                                  const VerticalLayoutParams& params, fb::Arena& scratch,
                                  PageSink& sink, uint32_t* pageCountOut,
                                  uint32_t* totalCharsOut) {
  if (params.font == nullptr) return BookStatus::Unsupported;

  const size_t mark = scratch.mark();
  VerticalEngine* engine = scratch.allocArray<VerticalEngine>(1);
  if (engine == nullptr) {
    scratch.release(mark);
    return BookStatus::OutOfMemory;
  }
  new (engine) VerticalEngine();
  if (!engine->init(params, scratch, sink, &source, &zip, chapterHref)) {
    scratch.release(mark);
    return BookStatus::OutOfMemory;
  }

  ChapterHandler handler(*engine, params, sink);
  BookStatus status = fb::XmlSax::parseEntry(source, entry, scratch, handler, true);
  if (status == BookStatus::Ok) {
    engine->finish();
    if (pageCountOut != nullptr) *pageCountOut = engine->pageCount();
    if (totalCharsOut != nullptr) *totalCharsOut = engine->totalChars();
  }
  engine->~VerticalEngine();
  scratch.release(mark);
  return status;
}

BookStatus VerticalLayout::layoutPlainText(fb::BookSource& source,
                                           const VerticalLayoutParams& params, fb::Arena& scratch,
                                           PageSink& sink, bool aozoraRuby, uint32_t* pageCountOut,
                                           uint32_t* totalCharsOut) {
  if (params.font == nullptr) return BookStatus::Unsupported;

  const size_t mark = scratch.mark();
  VerticalEngine* engine = scratch.allocArray<VerticalEngine>(1);
  char* buf = scratch.allocArray<char>(kTextChunk + 8);
  if (engine == nullptr || buf == nullptr) {
    scratch.release(mark);
    return BookStatus::OutOfMemory;
  }
  new (engine) VerticalEngine();
  if (!engine->init(params, scratch, sink)) {
    scratch.release(mark);
    return BookStatus::OutOfMemory;
  }

  uint64_t offset = 0;
  const uint64_t total = source.size();
  uint32_t carry = 0;  // 直前のチャンク末尾に残った不完全な UTF-8 バイト
  BookStatus status = BookStatus::Ok;
  while (offset < total && !engine->stopped()) {
    const uint32_t want = static_cast<uint32_t>(
        total - offset > kTextChunk ? kTextChunk : (total - offset));
    const int32_t got = source.readAt(offset, buf + carry, want);
    if (got < 0) {
      status = BookStatus::IoError;
      break;
    }
    if (got == 0) break;
    offset += static_cast<uint32_t>(got);
    uint32_t avail = carry + static_cast<uint32_t>(got);
    // UTF-8 の途中で切れていたら次のチャンクへ持ち越す。
    uint32_t keep = 0;
    while (keep < 3 && avail - keep > 0) {
      const uint8_t b = static_cast<uint8_t>(buf[avail - keep - 1]);
      if ((b & 0xC0) == 0x80) {
        ++keep;
        continue;
      }
      const uint32_t need = (b < 0x80) ? 0 : ((b & 0xE0) == 0xC0 ? 1 : ((b & 0xF0) == 0xE0 ? 2 : 3));
      if (need > keep) {
        ++keep;  // 先頭バイトも含めて持ち越す
      } else {
        keep = 0;
      }
      break;
    }
    if (offset >= total) keep = 0;
    const uint32_t use = avail - keep;
    if (aozoraRuby) {
      engine->addAozoraText(buf, use, 0);
    } else {
      engine->addText(buf, use, 0);
    }
    if (keep > 0) memmove(buf, buf + use, keep);
    carry = keep;
  }
  if (status == BookStatus::Ok) {
    engine->finish();
    if (pageCountOut != nullptr) *pageCountOut = engine->pageCount();
    if (totalCharsOut != nullptr) *totalCharsOut = engine->totalChars();
  }
  engine->~VerticalEngine();
  scratch.release(mark);
  return status;
}

}  // namespace jabook
