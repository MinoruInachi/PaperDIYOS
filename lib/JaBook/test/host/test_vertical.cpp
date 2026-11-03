// JaBook 縦組みエンジンのホストテスト。実機もフォントファイルも要らない
// （メトリクスだけを返す偽フォントで組む）。
//
//   lib/JaBook/test/host/run.sh

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <JaTextRules.h>
#include <JaVertical.h>
#include <cache/PageCache.h>

using namespace freeink::book;

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond)                                                             \
  do {                                                                          \
    ++gChecks;                                                                  \
    if (!(cond)) {                                                              \
      printf("  ✗ %s:%d  %s\n", __FILE__, __LINE__, #cond);                     \
      ++gFailures;                                                              \
    }                                                                           \
  } while (0)

#define CHECK_STR(a, b)                                                         \
  do {                                                                          \
    ++gChecks;                                                                  \
    const std::string va = (a);                                                 \
    const std::string vb = (b);                                                 \
    if (va != vb) {                                                             \
      printf("  ✗ %s:%d  %s == %s （\"%s\" と \"%s\"）\n", __FILE__, __LINE__, #a, #b, \
             va.c_str(), vb.c_str());                                           \
      ++gFailures;                                                              \
    }                                                                           \
  } while (0)

#define CHECK_EQ(a, b)                                                          \
  do {                                                                          \
    ++gChecks;                                                                  \
    const long va = static_cast<long>(a);                                       \
    const long vb = static_cast<long>(b);                                       \
    if (va != vb) {                                                             \
      printf("  ✗ %s:%d  %s == %s （%ld と %ld）\n", __FILE__, __LINE__, #a, #b, va, vb); \
      ++gFailures;                                                              \
    }                                                                           \
  } while (0)

namespace {

// 全角は 1em、半角は 0.5em を返すだけの偽フォント。組版の判定はすべて
// メトリクスで決まるので、これで版面の計算を厳密に確かめられる。
class FakeFont : public BookFont {
 public:
  int16_t advance(uint32_t cp, uint16_t sizePx, uint8_t) override {
    return jabook::isFullWidth(cp) ? static_cast<int16_t>(sizePx)
                                   : static_cast<int16_t>(sizePx / 2);
  }
  int16_t lineHeight(uint16_t sizePx) override { return static_cast<int16_t>(sizePx * 12 / 10); }
  int16_t ascent(uint16_t sizePx) override { return static_cast<int16_t>(sizePx * 88 / 100); }
};

class MemorySource : public BookSource {
 public:
  explicit MemorySource(const char* text) : data_(text), size_(strlen(text)) {}
  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override {
    if (offset >= size_) return 0;
    const uint32_t n = static_cast<uint32_t>(size_ - offset) < len
                           ? static_cast<uint32_t>(size_ - offset)
                           : len;
    memcpy(dst, data_ + offset, n);
    return static_cast<int32_t>(n);
  }
  uint64_t size() const override { return size_; }

 private:
  const char* data_;
  uint64_t size_;
};

// onPage で渡されるページは呼び出し中しか有効でないので、検証用に写す。
struct CapturedRun {
  std::string text;
  int16_t x;
  int16_t y;
  uint16_t sizePx;
  uint8_t flags;
  bool rotated() const { return (flags & jabook::VertRotated) != 0; }
  bool ruby() const { return (flags & jabook::VertRuby) != 0; }
};
struct CapturedPage {
  std::vector<CapturedRun> runs;
  uint32_t charStart;
};

class Collector : public PageSink {
 public:
  bool onPage(const Page& page) override {
    CapturedPage out;
    out.charStart = page.charStart;
    for (uint16_t i = 0; i < page.runCount; ++i) {
      const PageTextRun& r = page.runs[i];
      out.runs.push_back({std::string(r.text, r.len), r.x, r.baselineY, r.sizePx, r.styleFlags});
    }
    pages.push_back(std::move(out));
    return true;
  }
  std::vector<CapturedPage> pages;
};

FakeFont gFont;

jabook::VerticalLayoutParams defaultParams() {
  jabook::VerticalLayoutParams p;
  p.pageWidth = 480;
  p.pageHeight = 800;
  p.marginTop = 40;
  p.marginBottom = 40;
  p.marginLeft = 28;
  p.marginRight = 28;
  p.baseSizePx = 24;
  p.lineGapPct = 180;
  p.firstLineIndentTenths = 0;
  p.font = &gFont;
  return p;
}

Arena makeArena(size_t bytes) { return Arena(malloc(bytes), bytes); }

// 本文（ルビでない）run を、列ごとにまとめ直す。x が同じものが 1 列。
std::vector<std::vector<CapturedRun>> columnsOf(const CapturedPage& page) {
  std::vector<std::vector<CapturedRun>> columns;
  int16_t lastX = INT16_MIN;
  for (const CapturedRun& r : page.runs) {
    if (r.ruby()) continue;
    if (columns.empty() || r.x != lastX) {
      // 回転 run の x は文字枠左端、正立 run はグリフの左端（中央寄せ済み）。
      // 全角なら両者は一致し、半角は枠内に寄るので許容幅で同じ列とみなす。
      if (!columns.empty() && lastX != INT16_MIN && r.x >= lastX - 12 && r.x <= lastX + 12) {
        columns.back().push_back(r);
        continue;
      }
      columns.push_back({});
      lastX = r.x;
    }
    columns.back().push_back(r);
  }
  return columns;
}

std::string firstCharOf(const std::string& s) {
  uint32_t i = 0;
  const uint32_t cp = jabook::utf8Next(s.c_str(), s.size(), &i);
  char buf[5] = {0};
  const uint32_t n = jabook::utf8Encode(cp, buf);
  return std::string(buf, n);
}

// --- テスト -----------------------------------------------------------------

void testUtf8() {
  printf("UTF-8 の往復\n");
  const uint32_t samples[] = {0x41, 0xA9, 0x3042, 0x4E00, 0x1F600, 0xFF5C};
  for (uint32_t cp : samples) {
    char buf[8] = {0};
    const uint32_t n = jabook::utf8Encode(cp, buf);
    uint32_t i = 0;
    CHECK_EQ(jabook::utf8Next(buf, n, &i), cp);
    CHECK_EQ(i, n);
  }
  // 途中で切れた列は U+FFFD になり、必ず終端まで進む（無限ループしない）。
  const char truncated[] = "\xE3\x81";
  uint32_t i = 0;
  CHECK_EQ(jabook::utf8Next(truncated, 2, &i), 0xFFFD);
  CHECK_EQ(i, 2);
}

void testCharacterClasses() {
  printf("字の分類\n");
  CHECK(jabook::vertFormOf(0x3042) == jabook::VertForm::Upright);      // あ
  CHECK(jabook::vertFormOf(0x3001) == jabook::VertForm::PunctTopRight);// 、
  CHECK(jabook::vertFormOf(0x3002) == jabook::VertForm::PunctTopRight);// 。
  CHECK(jabook::vertFormOf(0x300C) == jabook::VertForm::Rotated);      // 「
  CHECK(jabook::vertFormOf(0x30FC) == jabook::VertForm::Rotated);      // ー
  CHECK(jabook::vertFormOf(0x301C) == jabook::VertForm::Rotated);      // 〜
  CHECK(jabook::vertFormOf(0x2026) == jabook::VertForm::Rotated);      // …
  CHECK(jabook::vertFormOf('A') == jabook::VertForm::Rotated);
  CHECK(jabook::vertFormOf(0x3063) == jabook::VertForm::SmallKana);    // っ
  CHECK(jabook::vertFormOf(0x30E3) == jabook::VertForm::SmallKana);    // ャ

  CHECK(jabook::isLineStartProhibited(0x3002, false));                 // 。
  CHECK(jabook::isLineStartProhibited(0x300D, false));                 // 」
  CHECK(!jabook::isLineStartProhibited(0x30FC, false));                // ー は標準では可
  CHECK(jabook::isLineStartProhibited(0x30FC, true));                  // 厳格では不可
  CHECK(jabook::isLineStartProhibited(0x3063, true));                  // っ
  CHECK(jabook::isLineEndProhibited(0x300C));                          // 「
  CHECK(!jabook::isLineEndProhibited(0x300D));                         // 」
}

void testColumnGeometry() {
  printf("版面の座標\n");
  // 「あ」を 90 個。1 列 30 字なので 3 列になるはず。
  std::string text;
  for (int i = 0; i < 90; ++i) text += "あ";
  MemorySource src(text.c_str());
  Arena scratch = makeArena(1 << 20);
  Collector sink;
  jabook::VerticalLayoutParams p = defaultParams();
  p.ruby = false;
  uint32_t pages = 0, chars = 0;
  CHECK(jabook::VerticalLayout::layoutPlainText(src, p, scratch, sink, false, &pages, &chars) ==
        BookStatus::Ok);
  CHECK_EQ(pages, 1);
  CHECK_EQ(chars, 90);
  CHECK_EQ(sink.pages.size(), 1);
  CHECK_EQ(sink.pages[0].runs.size(), 90);  // 正立の全角は 1 字 1 run

  const auto columns = columnsOf(sink.pages[0]);
  CHECK_EQ(columns.size(), 3);
  CHECK_EQ(columns[0].size(), 30);

  // 最右の列の文字枠左端は 480 - 28(余白) - 24(全角) = 428
  // （ルビなしなので右端の予約もない）
  CHECK_EQ(columns[0][0].x, 428);
  // 列は行送り 24*180% = 43 px ずつ左へ
  CHECK_EQ(columns[0][0].x - columns[1][0].x, 43);
  CHECK_EQ(columns[1][0].x - columns[2][0].x, 43);
  // 列の中は上から下へ 24 px ずつ、最初のベースラインは上余白 + 0.88em
  CHECK_EQ(columns[0][0].y, 40 + 21);
  CHECK_EQ(columns[0][1].y - columns[0][0].y, 24);
}

void testKinsoku() {
  printf("禁則処理\n");
  // 30 字目に「。」が来る配置。追い出して 29 字目から次列へ送るはず。
  std::string text;
  for (int i = 0; i < 29; ++i) text += "あ";
  text += "い。";
  for (int i = 0; i < 40; ++i) text += "う";
  MemorySource src(text.c_str());
  Arena scratch = makeArena(1 << 20);
  Collector sink;
  jabook::VerticalLayoutParams p = defaultParams();
  p.ruby = false;
  p.hangingPunctuation = false;  // ぶら下げを切って追い出しだけを見る
  CHECK(jabook::VerticalLayout::layoutPlainText(src, p, scratch, sink, false) == BookStatus::Ok);

  const auto columns = columnsOf(sink.pages[0]);
  CHECK(columns.size() >= 2);
  for (size_t c = 1; c < columns.size(); ++c) {
    const std::string head = firstCharOf(columns[c][0].text);
    uint32_t i = 0;
    const uint32_t cp = jabook::utf8Next(head.c_str(), head.size(), &i);
    CHECK(!jabook::isLineStartProhibited(cp, false));
  }
  // 追い出しの結果、1 列目は 29 字（「い」が次列の頭へ移る）
  CHECK_EQ(columns[0].size(), 29);
  CHECK_STR(firstCharOf(columns[1][0].text), "い");
}

void testHangingPunctuation() {
  printf("句読点のぶら下げ\n");
  std::string text;
  for (int i = 0; i < 29; ++i) text += "あ";
  text += "い。";
  for (int i = 0; i < 10; ++i) text += "う";
  MemorySource src(text.c_str());
  Arena scratch = makeArena(1 << 20);
  Collector sink;
  jabook::VerticalLayoutParams p = defaultParams();
  p.ruby = false;
  p.hangingPunctuation = true;
  CHECK(jabook::VerticalLayout::layoutPlainText(src, p, scratch, sink, false) == BookStatus::Ok);

  const auto columns = columnsOf(sink.pages[0]);
  // ぶら下げると 1 列目は 31 字（「。」が版面の外に出る）
  CHECK_EQ(columns[0].size(), 31);
  CHECK_STR(firstCharOf(columns[0][30].text), "。");
  // ぶら下げた「。」は下余白へはみ出す（＝版面の下端より下）
  CHECK(columns[0][30].y > 40 + 720 - 24);
}

void testRotationAndTateChuYoko() {
  printf("回転と縦中横\n");
  MemorySource src("あ「ー」12年と1234年とAB");
  Arena scratch = makeArena(1 << 20);
  Collector sink;
  jabook::VerticalLayoutParams p = defaultParams();
  p.ruby = false;
  CHECK(jabook::VerticalLayout::layoutPlainText(src, p, scratch, sink, false) == BookStatus::Ok);
  CHECK(!sink.pages.empty());

  const auto& runs = sink.pages[0].runs;
  auto findRun = [&runs](const char* text) -> const CapturedRun* {
    for (const CapturedRun& r : runs) {
      if (r.text == text) return &r;
    }
    return nullptr;
  };
  // 連続する回転文字はひとつの run にまとまるので、含む run を探す。
  auto findContaining = [&runs](const char* text) -> const CapturedRun* {
    for (const CapturedRun& r : runs) {
      if (r.text.find(text) != std::string::npos) return &r;
    }
    return nullptr;
  };

  const CapturedRun* a = findRun("あ");
  CHECK(a != nullptr && !a->rotated());
  const CapturedRun* open = findContaining("「");
  CHECK(open != nullptr && open->rotated());
  const CapturedRun* dash = findContaining("ー");
  CHECK(dash != nullptr && dash->rotated());
  // 2 桁は縦中横：1 run にまとまり、回転しない
  const CapturedRun* tcy = findRun("12");
  CHECK(tcy != nullptr && !tcy->rotated());
  // 4 桁は縦中横にせず、回転してそのまま流す
  const CapturedRun* year = findContaining("1234");
  CHECK(year != nullptr && year->rotated());
  // 欧文の語は 1 run にまとめて倒す
  const CapturedRun* latin = findContaining("AB");
  CHECK(latin != nullptr && latin->rotated());
  // 括弧・長音符・欧文が続くところは 1 run にまとまっている
  CHECK(findRun("あ") != nullptr);
}

void testPunctuationPlacement() {
  printf("約物の位置\n");
  MemorySource src("あ。い");
  Arena scratch = makeArena(1 << 20);
  Collector sink;
  jabook::VerticalLayoutParams p = defaultParams();
  p.ruby = false;
  CHECK(jabook::VerticalLayout::layoutPlainText(src, p, scratch, sink, false) == BookStatus::Ok);
  const auto& runs = sink.pages[0].runs;
  CHECK_EQ(runs.size(), 3);
  // 句読点は全角枠の右上へ寄る：x は +半角、ベースラインは -半角
  CHECK_EQ(runs[1].x - runs[0].x, 12);
  CHECK_EQ(runs[1].y - (runs[0].y + 24), -12);
  // 次の字は枠どおりに戻る
  CHECK_EQ(runs[2].x, runs[0].x);
  CHECK_EQ(runs[2].y - runs[0].y, 48);
}

void testRuby() {
  printf("ルビ\n");
  MemorySource src("｜吾輩《わがはい》は猫である");
  Arena scratch = makeArena(1 << 20);
  Collector sink;
  jabook::VerticalLayoutParams p = defaultParams();
  p.ruby = true;
  CHECK(jabook::VerticalLayout::layoutPlainText(src, p, scratch, sink, true) == BookStatus::Ok);
  CHECK(!sink.pages.empty());

  int rubyCount = 0;
  int16_t rubyX = 0;
  int16_t bodyX = 0;
  for (const CapturedRun& r : sink.pages[0].runs) {
    if (r.ruby()) {
      ++rubyCount;
      rubyX = r.x;
      CHECK_EQ(r.sizePx, 12);  // 本文 24px の 50%
    } else if (r.text == "吾") {
      bodyX = r.x;
    }
  }
  CHECK_EQ(rubyCount, 4);      // わ・が・は・い
  CHECK(rubyX > bodyX);        // ルビは本文列の右脇
  CHECK_EQ(rubyX, bodyX + 24); // ちょうど全角 1 字ぶん右

  // ルビの親文字は本文に流れる（《》とルビ本体は本文に出ない）
  std::string body;
  for (const CapturedRun& r : sink.pages[0].runs) {
    if (!r.ruby()) body += r.text;
  }
  CHECK(body.find("吾輩は猫である") != std::string::npos);
  CHECK(body.find("わがはい") == std::string::npos);
}

// 書き出したページキャッシュをそのまま読み返せること（＝実機のページ送り）。
class MemoryCache : public CacheStorage {
 public:
  bool exists(const char* name) override { return files_.count(name) > 0; }
  bool remove(const char* name) override { return files_.erase(name) > 0; }
  int64_t fileSize(const char* name) override {
    auto it = files_.find(name);
    return it == files_.end() ? -1 : static_cast<int64_t>(it->second.size());
  }
  int32_t readAt(const char* name, uint32_t offset, void* dst, uint32_t len) override {
    auto it = files_.find(name);
    if (it == files_.end() || offset > it->second.size()) return -1;
    const uint32_t n = std::min<uint32_t>(len, it->second.size() - offset);
    memcpy(dst, it->second.data() + offset, n);
    return static_cast<int32_t>(n);
  }
  bool beginWrite(const char* name) override {
    writing_ = name;
    buffer_.clear();
    return true;
  }
  bool write(const void* data, uint32_t len) override {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    buffer_.insert(buffer_.end(), p, p + len);
    return true;
  }
  bool endWrite() override {
    files_[writing_] = buffer_;
    return true;
  }
  int32_t readBackAt(uint32_t offset, void* dst, uint32_t len) override {
    if (offset > buffer_.size()) return -1;
    const uint32_t n = std::min<uint32_t>(len, buffer_.size() - offset);
    memcpy(dst, buffer_.data() + offset, n);
    return static_cast<int32_t>(n);
  }

 private:
  std::map<std::string, std::vector<uint8_t>> files_;
  std::string writing_;
  std::vector<uint8_t> buffer_;
};

void testCacheRoundTrip() {
  printf("ページキャッシュの往復\n");
  std::string text;
  for (int i = 0; i < 400; ++i) text += "あいうえお、かきくけこ。";
  MemorySource src(text.c_str());
  Arena scratch = makeArena(2 << 20);
  Arena index = makeArena(1 << 20);

  jabook::VerticalLayoutParams p = defaultParams();
  const uint32_t gen = jabook::verticalLayoutGenerationHash(p, 0xABCD1234u);
  char name[64];
  CHECK(pageCacheName(0, gen, name, sizeof(name)));

  MemoryCache cache;
  PageCacheWriter writer;
  CHECK(writer.begin(cache, name, gen, scratch));
  uint32_t pages = 0, chars = 0;
  CHECK(jabook::VerticalLayout::layoutPlainText(src, p, scratch, writer, false, &pages, &chars) ==
        BookStatus::Ok);
  writer.setTotalChars(chars);
  CHECK(writer.finish());
  CHECK(pages > 1);

  // 同じ世代なら開ける。別の世代なら Stale。
  PageCacheReader reader;
  CHECK(reader.open(cache, name, gen, index) == BookStatus::Ok);
  CHECK_EQ(reader.pageCount(), pages);
  CHECK_EQ(reader.totalChars(), chars);

  PageCacheReader stale;
  Arena index2 = makeArena(1 << 20);
  CHECK(stale.open(cache, name, gen ^ 1u, index2) == BookStatus::Stale);

  // 読み返したページの run が、組んだときと同じ座標・フラグで戻る。
  Collector fresh;
  Arena scratch2 = makeArena(2 << 20);
  MemorySource src2(text.c_str());
  CHECK(jabook::VerticalLayout::layoutPlainText(src2, p, scratch2, fresh, false) ==
        BookStatus::Ok);

  Arena readScratch = makeArena(1 << 20);
  for (uint32_t i = 0; i < reader.pageCount() && i < fresh.pages.size(); ++i) {
    const size_t mark = readScratch.mark();
    Page page{};
    CHECK(reader.readPage(i, readScratch, &page) == BookStatus::Ok);
    CHECK_EQ(page.runCount, fresh.pages[i].runs.size());
    CHECK_EQ(page.charStart, fresh.pages[i].charStart);
    for (uint16_t r = 0; r < page.runCount && r < fresh.pages[i].runs.size(); ++r) {
      const CapturedRun& want = fresh.pages[i].runs[r];
      CHECK_EQ(page.runs[r].x, want.x);
      CHECK_EQ(page.runs[r].baselineY, want.y);
      CHECK_EQ(page.runs[r].sizePx, want.sizePx);
      CHECK_EQ(page.runs[r].styleFlags, want.flags);
      CHECK(std::string(page.runs[r].text, page.runs[r].len) == want.text);
    }
    readScratch.release(mark);
  }

  // 文字位置からページを引ける（＝しおりと設定変更後の復帰）。
  const uint32_t mid = reader.charStart(reader.pageCount() / 2);
  CHECK_EQ(reader.pageForChar(mid), reader.pageCount() / 2);
  CHECK_EQ(reader.pageForChar(0), 0u);
}

void testGenerationHash() {
  printf("キャッシュ世代\n");
  jabook::VerticalLayoutParams a = defaultParams();
  jabook::VerticalLayoutParams b = a;
  CHECK_EQ(jabook::verticalLayoutGenerationHash(a, 1),
           jabook::verticalLayoutGenerationHash(b, 1));
  b.baseSizePx = 28;
  CHECK(jabook::verticalLayoutGenerationHash(a, 1) !=
        jabook::verticalLayoutGenerationHash(b, 1));
  b = a;
  b.ruby = !a.ruby;
  CHECK(jabook::verticalLayoutGenerationHash(a, 1) !=
        jabook::verticalLayoutGenerationHash(b, 1));
  // フォントを替えたら組み直す
  CHECK(jabook::verticalLayoutGenerationHash(a, 1) !=
        jabook::verticalLayoutGenerationHash(a, 2));
  // 横組みのハッシュとぶつからない
  LayoutParams h;
  h.pageWidth = a.pageWidth;
  h.pageHeight = a.pageHeight;
  h.baseSizePx = a.baseSizePx;
  CHECK(jabook::verticalLayoutGenerationHash(a, 1) != layoutGenerationHash(h, 1));
}

void testLongParagraph() {
  printf("長い段落（メモリ上限）\n");
  std::string text;
  for (int i = 0; i < 20000; ++i) text += "あ";
  MemorySource src(text.c_str());
  Arena scratch = makeArena(2 << 20);
  Collector sink;
  jabook::VerticalLayoutParams p = defaultParams();
  p.ruby = false;
  uint32_t pages = 0, chars = 0;
  CHECK(jabook::VerticalLayout::layoutPlainText(src, p, scratch, sink, false, &pages, &chars) ==
        BookStatus::Ok);
  CHECK_EQ(chars, 20000);
  // 1 ページ 10 列 × 30 字 = 300 字
  CHECK(pages >= 66 && pages <= 70);
  printf("  作業メモリの最大 %zu KB / %u ページ\n", scratch.highWater() / 1024, pages);
  // 段落が何万字あっても、常駐は「1 段落 + 1 ページ」ぶんに収まる。
  CHECK(scratch.highWater() < 400u * 1024);
}

}  // namespace

int main() {
  printf("JaBook 縦組みテスト\n\n");
  testUtf8();
  testCharacterClasses();
  testColumnGeometry();
  testKinsoku();
  testHangingPunctuation();
  testRotationAndTateChuYoko();
  testPunctuationPlacement();
  testRuby();
  testCacheRoundTrip();
  testGenerationHash();
  testLongParagraph();

  printf("\n%d 項目中 %d 失敗\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
