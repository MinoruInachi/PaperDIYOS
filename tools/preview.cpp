// JaBook プレビュー — 実機なしで組版結果を見るためのホストツール。
//
//   preview <本.epub|本.txt> <フォント.ttf> <出力ディレクトリ> [オプション]
//
// PaperDIY と同じ 540x960 の論理ページに組み、1 ページずつ PGM (P5) で
// 書き出す。縦組み（既定）と横組み（--yoko、FreeInkBook の ChapterLayout）を
// 同じ条件で比べられる。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <FreeInkBook.h>
#include <JaVertical.h>
#include <JaVerticalRenderer.h>
#include <layout/ChapterLayout.h>
#include <render/PageRenderer.h>
#include <render/TtfFont.h>

using namespace freeink::book;

namespace {

class FileSource : public BookSource {
 public:
  bool open(const char* path) {
    fp_ = fopen(path, "rb");
    if (fp_ == nullptr) return false;
    fseek(fp_, 0, SEEK_END);
    size_ = static_cast<uint64_t>(ftell(fp_));
    fseek(fp_, 0, SEEK_SET);
    return true;
  }
  ~FileSource() override {
    if (fp_ != nullptr) fclose(fp_);
  }
  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override {
    if (fseek(fp_, static_cast<long>(offset), SEEK_SET) != 0) return -1;
    return static_cast<int32_t>(fread(dst, 1, len, fp_));
  }
  uint64_t size() const override { return size_; }

 private:
  FILE* fp_ = nullptr;
  uint64_t size_ = 0;
};

struct Options {
  int16_t pageWidth = 540;
  int16_t pageHeight = 960;
  uint16_t sizePx = 24;
  uint16_t lineGapPct = 180;
  uint32_t maxPages = 6;
  uint32_t firstSpine = 0;
  bool vertical = true;
  bool ruby = true;
  const char* outDir = ".";
};

uint8_t* gFrame = nullptr;
Options gOpt;
uint32_t gWritten = 0;

void writePgm(uint32_t index) {
  char path[512];
  snprintf(path, sizeof(path), "%s/page%02u.pgm", gOpt.outDir, index);
  FILE* f = fopen(path, "wb");
  if (f == nullptr) {
    fprintf(stderr, "書き出せません: %s\n", path);
    return;
  }
  fprintf(f, "P5\n%d %d\n255\n", gOpt.pageWidth, gOpt.pageHeight);
  fwrite(gFrame, 1, static_cast<size_t>(gOpt.pageWidth) * gOpt.pageHeight, f);
  fclose(f);
  printf("  %s\n", path);
}

class RenderSink : public PageSink {
 public:
  RenderSink(FontChain& fonts, Arena& scratch, BookSource* src, const ZipCatalog* zip)
      : fonts_(fonts), scratch_(scratch), src_(src), zip_(zip) {}

  bool onPage(const Page& page) override {
    memset(gFrame, 0xFF, static_cast<size_t>(gOpt.pageWidth) * gOpt.pageHeight);
    FrameTarget target{};
    target.framebuffer = gFrame;
    target.width = gOpt.pageWidth;
    target.height = gOpt.pageHeight;
    target.widthBytes = gOpt.pageWidth;  // Gray8 は 1 画素 1 バイト
    target.format = FrameFormat::Gray8;
    target.rotation = FrameRotation::None;

    uint32_t missing = 0;
    uint32_t firstMissing = 0;
    if (gOpt.vertical) {
      jabook::VerticalRenderParams rp;
      missing = jabook::VerticalPageRenderer::renderText(page, fonts_, target, rp, &firstMissing);
      if (src_ != nullptr && zip_ != nullptr) {
        PageRenderer::renderImages(page, *src_, *zip_, scratch_, target);
      }
    } else {
      missing = PageRenderer::renderText(page, fonts_, target, &firstMissing);
      if (src_ != nullptr && zip_ != nullptr) {
        PageRenderer::renderImages(page, *src_, *zip_, scratch_, target);
      }
    }
    if (missing > 0) {
      printf("  （出せなかった字 %u 個、最初は U+%04X）\n", missing, firstMissing);
    }
    writePgm(gWritten++);
    return gWritten < gOpt.maxPages;
  }

 private:
  FontChain& fonts_;
  Arena& scratch_;
  BookSource* src_;
  const ZipCatalog* zip_;
};

bool endsWith(const char* s, const char* suffix) {
  const size_t ls = strlen(s), lt = strlen(suffix);
  return ls >= lt && strcasecmp(s + ls - lt, suffix) == 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr,
            "使い方: preview <本.epub|本.txt> <フォント.ttf> <出力先> "
            "[--size N] [--pages N] [--spine N] [--gap N] [--yoko] [--no-ruby]\n");
    return 2;
  }
  const char* bookPath = argv[1];
  const char* fontPath = argv[2];
  gOpt.outDir = argv[3];
  for (int i = 4; i < argc; ++i) {
    if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
      gOpt.sizePx = static_cast<uint16_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "--pages") == 0 && i + 1 < argc) {
      gOpt.maxPages = static_cast<uint32_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "--spine") == 0 && i + 1 < argc) {
      gOpt.firstSpine = static_cast<uint32_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "--gap") == 0 && i + 1 < argc) {
      gOpt.lineGapPct = static_cast<uint16_t>(atoi(argv[++i]));
    } else if (strcmp(argv[i], "--yoko") == 0) {
      gOpt.vertical = false;
    } else if (strcmp(argv[i], "--no-ruby") == 0) {
      gOpt.ruby = false;
    } else {
      fprintf(stderr, "知らないオプション: %s\n", argv[i]);
      return 2;
    }
  }

  // --- フォント -------------------------------------------------------------
  FILE* ff = fopen(fontPath, "rb");
  if (ff == nullptr) {
    fprintf(stderr, "フォントを開けません: %s\n", fontPath);
    return 1;
  }
  fseek(ff, 0, SEEK_END);
  const long fontLen = ftell(ff);
  fseek(ff, 0, SEEK_SET);
  uint8_t* fontBytes = static_cast<uint8_t*>(malloc(static_cast<size_t>(fontLen)));
  if (fread(fontBytes, 1, static_cast<size_t>(fontLen), ff) != static_cast<size_t>(fontLen)) {
    fprintf(stderr, "フォントを読めません\n");
    return 1;
  }
  fclose(ff);

  const size_t glyphArenaBytes = 1u << 20;
  Arena glyphArena(malloc(glyphArenaBytes), glyphArenaBytes);
  TtfFont face;
  if (!face.init(fontBytes, static_cast<uint32_t>(fontLen), glyphArena)) {
    fprintf(stderr, "TTF を解釈できません（TrueType/OpenType か確認してください）\n");
    return 1;
  }
  FontChain fonts;
  fonts.add(&face);
  printf("フォント: %s (%.2f MB)  行高 %d px @ %u px\n", fontPath, fontLen / 1048576.0,
         face.lineHeight(gOpt.sizePx), gOpt.sizePx);

  gFrame = static_cast<uint8_t*>(malloc(static_cast<size_t>(gOpt.pageWidth) * gOpt.pageHeight));

  const size_t bookArenaBytes = 512u * 1024;
  const size_t scratchBytes = 1024u * 1024;
  Arena bookArena(malloc(bookArenaBytes), bookArenaBytes);
  Arena scratch(malloc(scratchBytes), scratchBytes);

  jabook::VerticalLayoutParams vp;
  vp.pageWidth = gOpt.pageWidth;
  vp.pageHeight = gOpt.pageHeight;
  vp.baseSizePx = gOpt.sizePx;
  vp.lineGapPct = gOpt.lineGapPct;
  vp.ruby = gOpt.ruby;
  vp.font = &fonts;

  LayoutParams hp;
  hp.pageWidth = gOpt.pageWidth;
  hp.pageHeight = gOpt.pageHeight;
  hp.baseSizePx = gOpt.sizePx;
  hp.language = "ja";
  hp.defaultAlign = TextAlign::Justify;
  hp.font = &fonts;

  if (endsWith(bookPath, ".txt")) {
    FileSource src;
    if (!src.open(bookPath)) {
      fprintf(stderr, "開けません: %s\n", bookPath);
      return 1;
    }
    RenderSink sink(fonts, scratch, nullptr, nullptr);
    uint32_t pages = 0, chars = 0;
    const BookStatus st =
        gOpt.vertical
            ? jabook::VerticalLayout::layoutPlainText(src, vp, scratch, sink, true, &pages, &chars)
            : ChapterLayout::layoutPlainText(src, hp, scratch, sink, &pages, &chars);
    printf("%s: %u ページ / %u 字 (%s)\n", gOpt.vertical ? "縦組み" : "横組み", pages, chars,
           bookStatusName(st));
    return st == BookStatus::Ok ? 0 : 1;
  }

  FileSource src;
  if (!src.open(bookPath)) {
    fprintf(stderr, "開けません: %s\n", bookPath);
    return 1;
  }
  Book book;
  const BookStatus opened = book.open(src, bookArena, scratch);
  if (opened != BookStatus::Ok) {
    fprintf(stderr, "EPUB を開けません: %s\n", bookStatusName(opened));
    return 1;
  }
  printf("『%s』 %s / 言語 %s / %zu 章\n", book.metadata().title, book.metadata().author,
         book.metadata().language, book.spineCount());

  RenderSink sink(fonts, scratch, &src, &book.zip());
  for (size_t s = gOpt.firstSpine; s < book.spineCount() && gWritten < gOpt.maxPages; ++s) {
    const ManifestItem* item = book.spineItem(s);
    if (item == nullptr || item->href == nullptr) continue;
    const ZipEntry* entry = book.zip().find(item->href);
    if (entry == nullptr) continue;
    printf("章 %zu: %s\n", s, item->href);
    const size_t mark = scratch.mark();
    uint32_t pages = 0, chars = 0;
    const BookStatus st =
        gOpt.vertical ? jabook::VerticalLayout::layout(src, book.zip(), *entry, item->href, vp,
                                                       scratch, sink, &pages, &chars)
                      : ChapterLayout::layout(src, book.zip(), *entry, item->href, hp, scratch,
                                              sink, &pages, &chars);
    scratch.release(mark);
    printf("  %u ページ / %u 字 (%s)  arena 最大 %zu KB\n", pages, chars, bookStatusName(st),
           scratch.highWater() / 1024);
    if (st != BookStatus::Ok) return 1;
  }
  return 0;
}
