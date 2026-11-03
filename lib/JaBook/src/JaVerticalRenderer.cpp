#include "JaVerticalRenderer.h"

namespace jabook {
namespace {

using fb::FrameFormat;
using fb::FrameRotation;
using fb::FrameTarget;

// --- 画素の書き込み（FreeInkBook PageRenderer と同一の規約） ----------------

constexpr uint8_t kInkThreshold = 120;
constexpr uint8_t kInkSolid = 140;
constexpr uint8_t kInkFloor = 40;
constexpr uint8_t kBayer4[4][4] = {
    {15, 135, 45, 165}, {195, 75, 225, 105}, {60, 180, 30, 150}, {240, 120, 210, 90}};

inline bool toPanel(const FrameTarget& t, int32_t x, int32_t y, int32_t& px, int32_t& py) {
  const int32_t lw =
      t.rotation == FrameRotation::Portrait || t.rotation == FrameRotation::PortraitInverted
          ? t.height
          : t.width;
  const int32_t lh = lw == t.height ? t.width : t.height;
  if (x < 0 || y < 0 || x >= lw || y >= lh) return false;
  switch (t.rotation) {
    case FrameRotation::Portrait:  // 90° CW
      px = y;
      py = t.height - 1 - x;
      break;
    case FrameRotation::PortraitInverted:  // 90° CCW
      px = t.width - 1 - y;
      py = x;
      break;
    case FrameRotation::UpsideDown:
      px = t.width - 1 - x;
      py = t.height - 1 - y;
      break;
    case FrameRotation::None:
      px = x;
      py = y;
      break;
  }
  return true;
}

inline void setBlack(const FrameTarget& t, int32_t x, int32_t y) {
  int32_t px, py;
  if (!toPanel(t, x, y, px, py)) return;
  t.framebuffer[py * t.widthBytes + (px >> 3)] &= static_cast<uint8_t>(~(0x80u >> (px & 7)));
}

inline void inkPixel(const FrameTarget& t, int32_t x, int32_t y, uint8_t coverage) {
  if (coverage == 0) return;
  switch (t.format) {
    case FrameFormat::Gray8: {
      int32_t px, py;
      if (!toPanel(t, x, y, px, py)) return;
      uint8_t* dst = &t.framebuffer[py * t.widthBytes + px];
      const uint8_t lum = static_cast<uint8_t>(255 - coverage);
      if (lum < *dst) *dst = lum;
      break;
    }
    case FrameFormat::Mono1Sharp:
      if (coverage >= kInkThreshold) setBlack(t, x, y);
      break;
    case FrameFormat::Mono1Dithered: {
      if (coverage >= kInkSolid) {
        setBlack(t, x, y);
        break;
      }
      if (coverage < kInkFloor) break;
      const uint32_t boosted =
          (static_cast<uint32_t>(coverage - kInkFloor) * 255u) / (kInkSolid - kInkFloor);
      if (boosted >= kBayer4[y & 3][x & 3]) setBlack(t, x, y);
      break;
    }
  }
}

uint32_t decodeUtf8(const char* text, uint32_t len, uint32_t& i) {
  const uint8_t b0 = static_cast<uint8_t>(text[i]);
  uint32_t cp = b0;
  uint32_t extra = 0;
  if (b0 >= 0xF0) {
    cp = b0 & 0x07;
    extra = 3;
  } else if (b0 >= 0xE0) {
    cp = b0 & 0x0F;
    extra = 2;
  } else if (b0 >= 0xC0) {
    cp = b0 & 0x1F;
    extra = 1;
  }
  ++i;
  while (extra > 0 && i < len && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) {
    cp = (cp << 6) | (static_cast<uint8_t>(text[i]) & 0x3F);
    ++i;
    --extra;
  }
  return cp;
}

}  // namespace

uint32_t VerticalPageRenderer::renderText(const fb::Page& page, fb::FontChain& fonts,
                                          const FrameTarget& target,
                                          const VerticalRenderParams& params,
                                          uint32_t* firstMissingOut) {
  uint32_t missing = 0;
  for (uint16_t r = 0; r < page.runCount; ++r) {
    const fb::PageTextRun& run = page.runs[r];
    const bool rotated = (run.styleFlags & VertRotated) != 0;
    // 縦組みのビットはフォント選択に渡さない。
    const uint8_t style = static_cast<uint8_t>(run.styleFlags & 0x1F);

    // 回転 run のベースラインは、全角枠の中心に字面の中心が来る位置。
    // 枠の一辺は「全角の送り」であって run.sizePx ではない（stb のサイズ
    // 指定は ascent+descent 基準なので和文では両者がずれる）ため、
    // 「一」の送りから復元する。
    int32_t em = rotated ? fonts.advance(0x4E00, run.sizePx, 0) : 0;
    if (em <= 0) em = run.sizePx;
    const int32_t baselineX = run.x + em - em * params.emAscentPct / 100;

    int32_t pen = rotated ? run.baselineY : run.x;
    uint32_t i = 0;
    uint32_t prev = 0;
    while (i < run.len) {
      const uint32_t cp = decodeUtf8(run.text, run.len, i);
      if (prev != 0) pen += fonts.kerning(prev, cp, run.sizePx, style);
      uint8_t faceFlags = 0;
      freeink::font::RasterFont* font = fonts.fontFor(cp, style, &faceFlags);
      const freeink::font::GlyphBitmap* glyph = font != nullptr ? font->rasterize(cp, run.sizePx) : nullptr;
      if (glyph == nullptr && cp != ' ' && cp != 0xA0 && cp != 0x3000) {
        if (missing++ == 0 && firstMissingOut != nullptr) *firstMissingOut = cp;
      }
      if (glyph != nullptr) {
        const int strikes =
            (style & freeink::font::StyleBold) != 0 && (faceFlags & freeink::font::StyleBold) == 0 ? 2 : 1;
        for (int s = 0; s < strikes; ++s) {
          for (uint16_t gy = 0; gy < glyph->height; ++gy) {
            const uint8_t* srcRow = glyph->pixels + static_cast<uint32_t>(gy) * glyph->width;
            const int32_t v = glyph->yoff + gy;
            for (uint16_t gx = 0; gx < glyph->width; ++gx) {
              const int32_t u = glyph->xoff + gx;
              if (rotated) {
                // 時計回り 90 度：横組みの (u, v) が (baselineX - v, pen + u) へ。
                inkPixel(target, baselineX - v, pen + u + s, srcRow[gx]);
              } else {
                inkPixel(target, pen + u + s, run.baselineY + v, srcRow[gx]);
              }
            }
          }
        }
      }
      pen += fonts.advance(cp, run.sizePx, style);
      prev = cp;
    }
  }
  return missing;
}

}  // namespace jabook
