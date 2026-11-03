#include "Gfx.h"

#include <Arduino.h>
#include <driver/LgfxEpdDriver.h>
#include <stdlib.h>
#include <string.h>

#include "Font5x7.h"

namespace pdos {
namespace {

using freeink::GrayUpdate;
using freeink::LgfxEpdDriver;

uint32_t nextCodepoint(const char* s, size_t* i) {
  const uint8_t b0 = static_cast<uint8_t>(s[*i]);
  uint32_t cp = b0;
  int extra = 0;
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
  ++*i;
  while (extra-- > 0 && (static_cast<uint8_t>(s[*i]) & 0xC0) == 0x80) {
    cp = (cp << 6) | (static_cast<uint8_t>(s[*i]) & 0x3F);
    ++*i;
  }
  return cp;
}

// 行頭に置かない文字。折り返しのとき前の行へぶら下げる。
bool noLineStart(uint32_t cp) {
  switch (cp) {
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0xFF09:  // ）
    case 0x3011:  // 】
    case 0xFF01:  // ！
    case 0xFF1F:  // ？
    case 0x30FC:  // ー
    case 0x2026:  // …
    case ',':
    case '.':
    case ')':
      return true;
    default:
      return false;
  }
}

}  // namespace

void Gfx::begin() { canvas_ = LgfxEpdDriver::grayCanvas(); }

fb::FrameTarget Gfx::target() const {
  fb::FrameTarget t{};
  t.framebuffer = canvas_;
  t.width = kPanelW;
  t.height = kPanelH;
  t.widthBytes = kPanelW;
  t.format = fb::FrameFormat::Gray8;
  t.rotation = fb::FrameRotation::Portrait;
  return t;
}

void Gfx::clear(uint8_t pen) {
  if (canvas_ != nullptr) memset(canvas_, (pen > 15 ? 15 : pen) * 17, static_cast<size_t>(kPanelW) * kPanelH);
}

void Gfx::pixel(int16_t x, int16_t y) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  *at(x, y) = lum_;
}

void Gfx::fill(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int16_t ly = x; ly < x + w; ++ly) memset(at(ly, y), lum_, h);
}

void Gfx::frame(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t) {
  fill(x, y, w, t);
  fill(x, y + h - t, w, t);
  fill(x, y, t, h);
  fill(x + w - t, y, t, h);
}

void Gfx::invert(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int16_t lx = x; lx < x + w; ++lx) {
    uint8_t* p = at(lx, y);
    for (int16_t i = 0; i < h; ++i) p[i] = static_cast<uint8_t>(255 - p[i]);
  }
}

void Gfx::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness) {
  const int32_t dx = abs(x1 - x0), dy = -abs(y1 - y0);
  const int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int32_t err = dx + dy;
  const int16_t half = thickness / 2;
  for (;;) {
    if (thickness <= 1) {
      pixel(x0, y0);
    } else {
      fill(x0 - half, y0 - half, thickness, thickness);
    }
    if (x0 == x1 && y0 == y1) break;
    const int32_t e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void Gfx::circle(int16_t cx, int16_t cy, int16_t r, bool filled) {
  for (int16_t dy = -r; dy <= r; ++dy) {
    const int32_t rr = static_cast<int32_t>(r) * r - static_cast<int32_t>(dy) * dy;
    int16_t half = 0;
    while (static_cast<int32_t>(half + 1) * (half + 1) <= rr) ++half;
    if (filled) {
      fill(cx - half, cy + dy, half * 2 + 1, 1);
    } else {
      pixel(cx - half, cy + dy);
      pixel(cx + half, cy + dy);
      const int32_t next = static_cast<int32_t>(r) * r - static_cast<int32_t>(dy + 1) * (dy + 1);
      int16_t h2 = 0;
      while (next >= 0 && static_cast<int32_t>(h2 + 1) * (h2 + 1) <= next) ++h2;
      if (h2 < half - 1) {
        fill(cx - half, cy + dy, half - h2, 1);
        fill(cx + h2 + 1, cy + dy, half - h2, 1);
      }
    }
  }
}

void Gfx::bitmap(const uint8_t* bits, int16_t w, int16_t h, int16_t x, int16_t y, int16_t scale) {
  const int16_t rowBytes = (w + 7) / 8;
  for (int16_t row = 0; row < h; ++row) {
    const uint8_t* line = bits + row * rowBytes;
    for (int16_t col = 0; col < w; ++col) {
      if ((line[col >> 3] & (0x80 >> (col & 7))) == 0) continue;
      if (scale == 1) {
        pixel(x + col, y + row);
      } else {
        fill(x + col * scale, y + row * scale, scale, scale);
      }
    }
  }
}

void Gfx::gray(const uint8_t* pixels, int16_t w, int16_t h, int16_t x, int16_t y) {
  for (int16_t row = 0; row < h; ++row) {
    const int16_t dy = y + row;
    if (dy < 0 || dy >= H) continue;
    for (int16_t col = 0; col < w; ++col) {
      const int16_t dx = x + col;
      if (dx < 0 || dx >= W) continue;
      *at(dx, dy) = pixels[static_cast<int32_t>(row) * w + col];
    }
  }
}

uint16_t Gfx::sizeForEm(int16_t em) {
  if (fonts_ == nullptr) return static_cast<uint16_t>(em);
  if (em == cachedEm_) return cachedSize_;
  uint16_t size = static_cast<uint16_t>(em);
  for (int i = 0; i < 4; ++i) {
    const int16_t adv = fonts_->advance(0x4E00, size, 0);
    if (adv <= 0) break;
    const int32_t next = (static_cast<int32_t>(em) * size + adv / 2) / adv;
    if (next <= 0 || next == size) break;
    size = static_cast<uint16_t>(next);
  }
  cachedEm_ = em;
  cachedSize_ = size;
  return size;
}

void Gfx::asciiChar(char c, int16_t x, int16_t y, int16_t em) {
  const int16_t scale = em >= 8 ? em / 8 : 1;
  const uint8_t index = static_cast<uint8_t>(c) - 0x20;
  const uint8_t* glyph = kFont5x7[index >= 95 ? '?' - 0x20 : index];
  for (int16_t col = 0; col < 5; ++col) {
    for (int16_t row = 0; row < 7; ++row) {
      if (glyph[col] & (1 << row)) fill(x + col * scale, y - (7 - row) * scale, scale, scale);
    }
  }
}

int16_t Gfx::text(const char* s, int16_t x, int16_t y, int16_t em) {
  if (s == nullptr || s[0] == '\0') return 0;
  if (fonts_ == nullptr) {
    const int16_t scale = em >= 8 ? em / 8 : 1;
    int16_t pen = x;
    for (const char* p = s; *p != '\0'; ++p) {
      if (static_cast<uint8_t>(*p) >= 0x80) continue;
      asciiChar(*p, pen, y, em);
      pen += 6 * scale;
    }
    return pen - x;
  }
  const uint16_t size = sizeForEm(em);
  int32_t pen = x;
  uint32_t prev = 0;
  size_t i = 0;
  while (s[i] != '\0') {
    const uint32_t cp = nextCodepoint(s, &i);
    if (prev != 0) pen += fonts_->kerning(prev, cp, size, 0);
    freeink::font::RasterFont* font = fonts_->fontFor(cp, 0, nullptr);
    const freeink::font::GlyphBitmap* glyph = font != nullptr ? font->rasterize(cp, size) : nullptr;
    if (glyph != nullptr) {
      for (uint16_t gy = 0; gy < glyph->height; ++gy) {
        const int32_t py = y + glyph->yoff + gy;
        if (py < 0 || py >= H) continue;
        const uint8_t* row = glyph->pixels + static_cast<uint32_t>(gy) * glyph->width;
        for (uint16_t gx = 0; gx < glyph->width; ++gx) {
          const uint8_t cov = row[gx];
          if (cov == 0) continue;
          const int32_t px = pen + glyph->xoff + gx;
          if (px < 0 || px >= W) continue;
          uint8_t* d = at(static_cast<int16_t>(px), static_cast<int16_t>(py));
          *d = static_cast<uint8_t>(*d + (static_cast<int32_t>(lum_) - *d) * cov / 255);
        }
      }
    }
    pen += fonts_->advance(cp, size, 0);
    prev = cp;
  }
  return static_cast<int16_t>(pen - x);
}

int16_t Gfx::textWidth(const char* s, int16_t em) {
  if (s == nullptr) return 0;
  if (fonts_ == nullptr) {
    const int16_t scale = em >= 8 ? em / 8 : 1;
    return static_cast<int16_t>(strlen(s) * 6 * scale);
  }
  const uint16_t size = sizeForEm(em);
  int32_t width = 0;
  size_t i = 0;
  while (s[i] != '\0') width += fonts_->advance(nextCodepoint(s, &i), size, 0);
  return static_cast<int16_t>(width);
}

int16_t Gfx::textVertical(const char* s, int16_t x, int16_t y, int16_t em) {
  size_t i = 0;
  while (s[i] != '\0') {
    const size_t start = i;
    const uint32_t cp = nextCodepoint(s, &i);
    char one[5] = {};
    memcpy(one, s + start, i - start < 4 ? i - start : 4);
    // 句読点は全角枠の右上へ寄せる。
    const bool mark = cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF0E;
    const int16_t lift = mark ? em * 6 / 10 : 0;
    text(one, x + lift, y + em - lift, em);
    y += em + em / 8;
  }
  return y;
}

void Gfx::textCentered(const char* s, int16_t cx, int16_t y, int16_t em) {
  text(s, cx - textWidth(s, em) / 2, y, em);
}

void Gfx::textRight(const char* s, int16_t right, int16_t y, int16_t em) {
  text(s, right - textWidth(s, em), y, em);
}

int16_t Gfx::textWrap(const char* s, int16_t x, int16_t y, int16_t w, int16_t em, int16_t lineHeight,
                      int16_t maxLines) {
  if (s == nullptr) return y;
  if (lineHeight == 0) lineHeight = em * 3 / 2;
  const uint16_t size = sizeForEm(em);
  char line[256];
  size_t start = 0;
  int16_t lines = 0;
  while (s[start] != '\0') {
    size_t i = start, lastFit = start;
    size_t lastSpace = start;  // 英文は語の途中で切らないように覚えておく
    int32_t width = 0;
    bool overflowed = false;
    while (s[i] != '\0' && s[i] != '\n') {
      size_t next = i;
      const uint32_t cp = nextCodepoint(s, &next);
      const int16_t adv = fonts_ != nullptr ? fonts_->advance(cp, size, 0) : (em / 8 > 0 ? em / 8 : 1) * 6;
      // 行頭禁則の字は幅を超えてもこの行に残す（ぶら下げ）。
      if (width + adv > w && i > start && !noLineStart(cp)) {
        overflowed = true;
        break;
      }
      width += adv;
      i = next;
      lastFit = i;
      if (cp == ' ') lastSpace = i;
      if (lastFit - start >= sizeof(line) - 5) break;
    }
    if (overflowed && lastSpace > start && static_cast<uint8_t>(s[i]) < 0x80 && s[i] != ' ') {
      // 次の字が英数字なら語の途中なので、最後の空白まで戻す。
      lastFit = lastSpace;
    }
    const size_t len = lastFit - start;
    memcpy(line, s + start, len);
    line[len] = '\0';
    text(line, x, y, em);
    y += lineHeight;
    ++lines;
    start = lastFit;
    if (s[start] == '\n') ++start;
    if (maxLines > 0 && lines >= maxLines) break;
  }
  return y;
}

void Gfx::quantize() {
  const size_t total = static_cast<size_t>(kPanelW) * kPanelH;
  for (size_t i = 0; i < total; ++i) canvas_[i] = canvas_[i] >= 128 ? 0xFF : 0x00;
}

void Gfx::flushDeferred() {
  if (deferred_) update(Update::Fast);
}

void Gfx::update(Update mode) {
  if (canvas_ == nullptr) return;
  if (deferFast_ && mode == Update::Fast) {
    deferred_ = true;
    return;
  }
  deferred_ = false;
  // 起動直後はパネルの状態が分からないので、Fast だと前の絵が残る。
  if (!pushed_ && (mode == Update::Fast || mode == Update::Fastest)) mode = Update::Text;
  if (mode == Update::Fast || mode == Update::Fastest) quantize();
  const uint32_t start = millis();
  GrayUpdate g = GrayUpdate::Quality;
  switch (mode) {
    case Update::Quality: g = GrayUpdate::Quality; break;
    case Update::Text: g = GrayUpdate::Text; break;
    case Update::Fast: g = GrayUpdate::Fast; break;
    case Update::Fastest: g = GrayUpdate::Fastest; break;
  }
  LgfxEpdDriver::pushGrayCanvas(g);
  pushed_ = true;
  lastUpdateMs_ = millis() - start;
}

}  // namespace pdos
