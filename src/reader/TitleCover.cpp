#include "TitleCover.h"

#include <string.h>

namespace paperdiy {
namespace {

namespace fb = freeink::book;

// 紙の縁の余白と枠線の太さは、紙の大きさから決める。本棚の小さな表紙と
// 見開きの扉で同じ太さにすると、片方が不格好になる。
uint16_t borderFor(uint16_t width) {
  const uint16_t b = static_cast<uint16_t>(width / 38);
  return b < 2 ? 2 : b;
}
uint16_t marginFor(uint16_t width) {
  const uint16_t m = static_cast<uint16_t>(width / 15);
  return m < 5 ? 5 : m;
}
// 文字の濃さがこれ以上なら黒として置く。
//
// 白黒 2 値しかないので、境目をどこに置くかで太さが決まる。表紙の題名は
// 12〜26px と小さく、輪郭のほとんどが中間の濃さになる。半分（128）で切ると
// 線が痩せて消えるので、低め＝太めに寄せる。
constexpr uint8_t kInkThreshold = 80;

// この紙は「1 = 白」で持っているので、黒を置くときはビットを落とす。
inline void inkPixel(uint8_t* rows, uint16_t rowBytes, uint16_t w, uint16_t h, int32_t x,
                     int32_t y) {
  if (x < 0 || y < 0 || x >= static_cast<int32_t>(w) || y >= static_cast<int32_t>(h)) return;
  rows[static_cast<size_t>(y) * rowBytes + (x >> 3)] &=
      static_cast<uint8_t>(~(0x80u >> (x & 7)));
}

void fillRect(uint8_t* rows, uint16_t rowBytes, uint16_t w, uint16_t h, int32_t x0, int32_t y0,
              int32_t x1, int32_t y1) {
  for (int32_t y = y0; y < y1; ++y) {
    for (int32_t x = x0; x < x1; ++x) inkPixel(rows, rowBytes, w, h, x, y);
  }
}

// UTF-8 を 1 文字取り出す。進んだバイト数を返す（0 なら終わり）。
size_t utf8Next(const char* s, uint32_t* out) {
  const uint8_t c = static_cast<uint8_t>(s[0]);
  if (c == 0) return 0;
  if (c < 0x80) {
    *out = c;
    return 1;
  }
  uint8_t need = 0;
  uint32_t code = 0;
  if ((c & 0xE0) == 0xC0) {
    need = 1;
    code = c & 0x1Fu;
  } else if ((c & 0xF0) == 0xE0) {
    need = 2;
    code = c & 0x0Fu;
  } else if ((c & 0xF8) == 0xF0) {
    need = 3;
    code = c & 0x07u;
  } else {
    *out = '?';  // 続きのバイトが単体で来た。飛ばす
    return 1;
  }
  for (uint8_t i = 1; i <= need; ++i) {
    const uint8_t n = static_cast<uint8_t>(s[i]);
    if ((n & 0xC0) != 0x80) {  // 途中で切れている
      *out = '?';
      return 1;
    }
    code = (code << 6) | (n & 0x3Fu);
  }
  *out = code;
  return static_cast<size_t>(need) + 1;
}

uint16_t charCount(const char* s) {
  uint16_t n = 0;
  uint32_t cp = 0;
  for (size_t i = 0; s[i] != '\0';) {
    const size_t step = utf8Next(s + i, &cp);
    if (step == 0) break;
    i += step;
    ++n;
  }
  return n;
}

// 縦書きの 1 文字。セル（size×size）の真ん中に置く。
//
// ベースラインではなくセルの中心に合わせるのは、縦書きでは漢字とかなの
// 字面がほぼ揃っていて、そのほうが列がまっすぐ通って見えるため。
void blitCell(uint8_t* rows, uint16_t rowBytes, uint16_t w, uint16_t h, fb::FontChain& fonts,
              uint32_t cp, uint16_t size, int32_t cellCenterX, int32_t cellTopY) {
  freeink::font::RasterFont* face = fonts.fontFor(cp);
  if (face == nullptr) return;
  const freeink::font::GlyphBitmap* g = face->rasterize(cp, size);
  if (g == nullptr || g->pixels == nullptr || g->width == 0 || g->height == 0) return;

  const int32_t gx = cellCenterX - g->width / 2;
  const int32_t gy = cellTopY + (static_cast<int32_t>(size) - g->height) / 2;
  for (uint16_t row = 0; row < g->height; ++row) {
    const uint8_t* src = g->pixels + static_cast<size_t>(row) * g->width;
    for (uint16_t col = 0; col < g->width; ++col) {
      if (src[col] >= kInkThreshold) inkPixel(rows, rowBytes, w, h, gx + col, gy + row);
    }
  }
}

// 文字列を縦書きで流す。右の列から左へ。入りきらなければそこで打ち切る。
// 使った列の左端（次に書き始められる x）を返す。
int32_t runVertical(uint8_t* rows, uint16_t rowBytes, uint16_t w, uint16_t h,
                    fb::FontChain& fonts, const char* text, uint16_t size, int32_t rightX,
                    int32_t leftLimit, int32_t topY, int32_t bottomY) {
  const int32_t colStep = size + size / 6;  // 列と列のあいだを少し空ける
  const int32_t perCol = (bottomY - topY) / size;
  if (perCol <= 0) return rightX;

  int32_t colRight = rightX;
  int32_t inCol = 0;
  uint32_t cp = 0;
  for (size_t i = 0; text[i] != '\0';) {
    const size_t step = utf8Next(text + i, &cp);
    if (step == 0) break;
    i += step;

    if (inCol >= perCol) {  // この列は埋まった。左へ移る
      colRight -= colStep;
      inCol = 0;
    }
    if (colRight - size < leftLimit) break;  // もう左に余地がない

    blitCell(rows, rowBytes, w, h, fonts, cp, size, colRight - size / 2, topY + inCol * size);
    ++inCol;
  }
  return colRight - colStep;
}

}  // namespace

bool drawTitleCover(const char* title, const char* author, uint16_t width, uint16_t height,
                    uint16_t rowBytes, uint8_t* rows, fb::FontChain& fonts, uint16_t maxSize) {
  if (rows == nullptr || width == 0 || height == 0) return false;
  if (title == nullptr || title[0] == '\0') return false;

  // まず真っ白に（1 = 白）。
  memset(rows, 0xFF, static_cast<size_t>(rowBytes) * height);

  // 縁に枠を引く。これだけで「表紙らしさ」が出て、一覧でも本の形に見える。
  const uint16_t kBorder = borderFor(width);
  const uint16_t kMargin = marginFor(width);
  fillRect(rows, rowBytes, width, height, 0, 0, width, kBorder);
  fillRect(rows, rowBytes, width, height, 0, height - kBorder, width, height);
  fillRect(rows, rowBytes, width, height, 0, 0, kBorder, height);
  fillRect(rows, rowBytes, width, height, width - kBorder, 0, width, height);

  const int32_t left = kBorder + kMargin;
  const int32_t right = width - kBorder - kMargin;
  const int32_t top = kBorder + kMargin;
  const int32_t bottom = height - kBorder - kMargin;
  const int32_t usableW = right - left;
  const int32_t usableH = bottom - top;
  if (usableW <= 8 || usableH <= 8) return false;

  // 題名が収まる、いちばん大きい文字を選ぶ。大きいほうから試して、
  // 列の数が幅に収まったところで決める。
  //
  // **題名を先に決める。**この紙は 75x126 しかなく、一覧でどの本かを見分ける
  // 手がかりはほぼ題名だけ。著者のために列を先取りすると、そのぶん題名が
  // 一段小さくなってしまう。著者は題名を組んだあとの余りに入れる。
  const uint16_t n = charCount(title);
  uint16_t size = 8;
  for (uint16_t candidate = maxSize < 8 ? 8 : maxSize; candidate >= 8; --candidate) {
    const int32_t perCol = usableH / candidate;
    if (perCol <= 0) continue;
    const int32_t cols = (n + perCol - 1) / perCol;
    const int32_t colStep = candidate + candidate / 6;
    if (cols * colStep <= usableW) {
      size = candidate;
      break;
    }
  }

  const int32_t after =
      runVertical(rows, rowBytes, width, height, fonts, title, size, right, left, top, bottom);

  // 余った幅に著者を入れる。1 列ぶんも残っていなければ出さない。
  if (author != nullptr && author[0] != '\0') {
    // 著者は 1 列に収める。題名より小さくしたうえで、名前がぜんぶ入る
    // 大きさまで下げる（「コナン・ドイル」の「ル」だけ落ちるのを防ぐ）。
    const uint16_t an = charCount(author);
    uint16_t authorSize = static_cast<uint16_t>(size > 12 ? size * 2 / 3 : size);
    while (authorSize > 8 && an * authorSize > usableH) --authorSize;

    if (after - authorSize >= left) {
      // 下寄せ。題名と頭を揃えると、どちらが題名か分からなくなる。
      const int32_t need = an * authorSize;
      const int32_t authorTop = need <= usableH ? bottom - need : top;
      runVertical(rows, rowBytes, width, height, fonts, author, authorSize, after, left,
                  authorTop, bottom);
    }
  }
  return true;
}

}  // namespace paperdiy
