#include "I18n.h"
#include "Ui.h"

#include <M5GFX.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>
#include <lgfx/utility/lgfx_qrcode.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "Os.h"

namespace pdos {
namespace ui {

void header(const char* title) {
  Gfx& g = os.gfx();
  char battery[16] = "";
  const int16_t pct = os.batteryPercent();
  if (pct >= 0) snprintf(battery, sizeof(battery), TR("電池 %d%%", "Battery %d%%"), pct);
  // 文字は黒で描いて、帯ごと反転させる。
  g.setPen(15);
  g.fill(0, 0, Gfx::W, kHeaderH);
  g.setPen(0);
  // 題が長いときは電池をあきらめ、それでも入らなければ末尾を落とす。
  const int16_t batteryW = g.textWidth(battery, 22);
  const bool showBattery = g.textWidth(title, 28) + batteryW + kMargin * 3 <= Gfx::W;
  char clipped[96];
  snprintf(clipped, sizeof(clipped), "%s", title);
  size_t len = strlen(clipped);
  while (len > 0 && g.textWidth(clipped, 28) > Gfx::W - kMargin * 2) {
    --len;
    while (len > 0 && (static_cast<uint8_t>(clipped[len]) & 0xC0) == 0x80) --len;
    clipped[len] = '\0';
  }
  g.text(clipped, kMargin, 37, 28);
  if (showBattery) g.textRight(battery, Gfx::W - kMargin, 35, 22);
  g.invert(0, 0, Gfx::W, kHeaderH);
}

void footer(const char* hints) {
  Gfx& g = os.gfx();
  g.setPen(0);
  g.hline(0, Gfx::H - kFooterH, Gfx::W, 2);
  int16_t em = 20;
  while (em > 14 && g.textWidth(hints, em) > Gfx::W - kMargin * 2) --em;
  g.text(hints, kMargin, Gfx::H - 14, em);
}

void message(const char* line1, const char* line2) {
  Gfx& g = os.gfx();
  g.clear();
  g.setPen(0);
  g.textCentered(line1, Gfx::W / 2, Gfx::H / 2 - 20, 28);
  if (line2 != nullptr) g.textCentered(line2, Gfx::W / 2, Gfx::H / 2 + 28, 22);
  g.update(Update::Text);
}

int16_t qr(const char* text, int16_t x, int16_t y, int16_t size) {
  // 入る中で一番小さい版を探す。版が大きいほどモジュールは細かくなる。
  for (uint8_t version = 1; version <= 25; ++version) {
    const uint16_t bytes = lgfx_qrcode_getBufferSize(version);
    uint8_t* modules = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (modules == nullptr) return 0;
    QRCode code;
    if (lgfx_qrcode_initText(&code, modules, version, ECC_LOW, text) != 0) {
      heap_caps_free(modules);
      continue;
    }
    const int16_t scale = size / (code.size + 2);  // 周囲に 1 モジュールの余白
    if (scale < 1) {
      heap_caps_free(modules);
      return 0;
    }
    const int16_t side = scale * (code.size + 2);
    Gfx& g = os.gfx();
    g.setPen(15);
    g.fill(x, y, side, side);
    g.setPen(0);
    for (uint8_t my = 0; my < code.size; ++my) {
      for (uint8_t mx = 0; mx < code.size; ++mx) {
        if (lgfx_qrcode_getModule(&code, mx, my)) {
          g.fill(x + (mx + 1) * scale, y + (my + 1) * scale, scale, scale);
        }
      }
    }
    heap_caps_free(modules);
    return side;
  }
  return 0;
}

uint8_t* decodeImage(const char* path, int16_t w, int16_t h) {
  auto& sd = SDCardManager::getInstance();
  FsFile f = sd.open(path, O_RDONLY);
  if (!f) return nullptr;
  constexpr uint32_t kMaxFile = 5u * 1024 * 1024;
  const uint32_t size = static_cast<uint32_t>(f.fileSize());
  if (size == 0 || size > kMaxFile) return nullptr;
  uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
  if (data == nullptr) return nullptr;
  const bool read = f.read(data, size) == static_cast<int>(size);
  f.close();
  if (!read) {
    heap_caps_free(data);
    return nullptr;
  }

  lgfx::LGFX_Sprite sprite;
  sprite.setPsram(true);
  sprite.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  uint8_t* out = nullptr;
  if (sprite.createSprite(w, h) != nullptr) {
    sprite.fillScreen(0xFFFFFFu);
    const char* ext = strrchr(path, '.');
    bool ok = false;
    // 倍率 0 は「箱に収まるように縮める」。中央へ揃える。
    constexpr float kFit = 0.0f;
    const auto datum = lgfx::textdatum_t::middle_center;
    if (ext != nullptr && strcasecmp(ext, ".png") == 0) {
      ok = sprite.drawPng(data, size, 0, 0, w, h, 0, 0, kFit, kFit, datum);
    } else if (ext != nullptr && strcasecmp(ext, ".bmp") == 0) {
      ok = sprite.drawBmp(data, size, 0, 0, w, h, 0, 0, kFit, kFit, datum);
    } else {
      ok = sprite.drawJpg(data, size, 0, 0, w, h, 0, 0, kFit, kFit, datum);
    }
    if (ok) {
      out = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(w) * h, MALLOC_CAP_SPIRAM));
      if (out != nullptr) memcpy(out, sprite.getBuffer(), static_cast<size_t>(w) * h);
    }
    sprite.deleteSprite();
  }
  heap_caps_free(data);
  return out;
}

bool imageSize(const char* path, int16_t* w, int16_t* h) {
  FsFile f = SDCardManager::getInstance().open(path, O_RDONLY);
  if (!f) return false;
  uint8_t head[32];
  if (f.read(head, sizeof(head)) < 26) return false;
  // PNG: IHDR の幅と高さ
  if (head[0] == 0x89 && head[1] == 'P') {
    *w = static_cast<int16_t>((head[18] << 8) | head[19]);
    *h = static_cast<int16_t>((head[22] << 8) | head[23]);
    return true;
  }
  // BMP: 情報ヘッダ
  if (head[0] == 'B' && head[1] == 'M') {
    *w = static_cast<int16_t>(head[18] | (head[19] << 8));
    const int32_t raw = static_cast<int32_t>(head[22] | (head[23] << 8) | (head[24] << 16) | (static_cast<uint32_t>(head[25]) << 24));
    *h = static_cast<int16_t>(raw < 0 ? -raw : raw);
    return true;
  }
  // JPEG: SOF のセグメントまで読み飛ばす
  if (head[0] != 0xFF || head[1] != 0xD8) return false;
  f.seekSet(2);
  for (;;) {
    uint8_t seg[9];
    if (f.read(seg, 4) != 4 || seg[0] != 0xFF) return false;
    const uint8_t marker = seg[1];
    const uint16_t len = static_cast<uint16_t>((seg[2] << 8) | seg[3]);
    const bool sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
    if (sof) {
      if (f.read(seg, 5) != 5) return false;
      *h = static_cast<int16_t>((seg[1] << 8) | seg[2]);
      *w = static_cast<int16_t>((seg[3] << 8) | seg[4]);
      return true;
    }
    if (!f.seekCur(len - 2)) return false;
  }
}

void ListView::reset(uint16_t count, int16_t selected) {
  count_ = count;
  top_ = 0;
  select(selected);
}

void ListView::setCount(uint16_t count) {
  count_ = count;
  select(selected_);
}

void ListView::select(int16_t index) {
  if (count_ == 0) {
    selected_ = 0;
    return;
  }
  if (index < 0) index = 0;
  if (index >= count_) index = count_ - 1;
  selected_ = index;
}

void ListView::up() {
  if (count_ == 0) return;
  selected_ = selected_ == 0 ? count_ - 1 : selected_ - 1;
}

void ListView::down() {
  if (count_ == 0) return;
  selected_ = selected_ + 1 >= count_ ? 0 : selected_ + 1;
}

void ListView::draw(LabelFn label, void* user, int16_t rowH, int16_t em) {
  Gfx& g = os.gfx();
  const int16_t areaTop = kHeaderH + 8;
  const int16_t areaH = Gfx::H - kFooterH - 8 - areaTop;
  const int16_t rows = areaH / rowH;
  if (selected_ < top_) top_ = selected_;
  if (selected_ >= top_ + rows) top_ = selected_ - rows + 1;
  if (top_ < 0) top_ = 0;

  char buf[192];
  for (int16_t r = 0; r < rows && top_ + r < count_; ++r) {
    const int16_t index = top_ + r;
    const int16_t y = areaTop + r * rowH;
    buf[0] = '\0';
    label(static_cast<uint16_t>(index), buf, sizeof(buf), user);
    // 長い行は 2 段目へ入れず切る。一覧は 1 行 1 項目で見渡せるほうがよい。
    g.setPen(0);
    const int16_t maxW = Gfx::W - kMargin * 2 - 16;
    if (g.textWidth(buf, em) > maxW) {
      size_t len = strlen(buf);
      while (len > 0 && g.textWidth(buf, em) > maxW - em) {
        --len;
        while (len > 0 && (static_cast<uint8_t>(buf[len]) & 0xC0) == 0x80) --len;
        buf[len] = '\0';
      }
      strncat(buf, "…", sizeof(buf) - strlen(buf) - 1);
    }
    g.text(buf, kMargin + 8, y + rowH / 2 + em * 2 / 5, em);
    g.hline(kMargin, y + rowH - 1, Gfx::W - kMargin * 2, 1);
    if (index == selected_) g.invert(kMargin, y, Gfx::W - kMargin * 2, rowH - 1);
  }

  // 件数が 1 画面を超えるときは、右端に位置を出す。
  if (count_ > rows && rows > 0) {
    const int16_t trackH = rows * rowH;
    const int16_t barH = trackH * rows / count_ < 16 ? 16 : trackH * rows / count_;
    const int16_t barY = areaTop + (trackH - barH) * top_ / (count_ - rows);
    g.setPen(0);
    g.fill(Gfx::W - 12, barY, 6, barH);
  }
}

}  // namespace ui
}  // namespace pdos
