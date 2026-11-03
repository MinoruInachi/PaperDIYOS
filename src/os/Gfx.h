#pragma once

// 描画の窓口。Badger の display に当たる。
//
// 描く先は SDK の LgfxEpdDriver が持っている 8bit グレーのキャンバス
// （パネル座標 960x540）で、座標は縦持ちの論理 540x960。向きは FreeInkBook の
// FrameRotation::Portrait と揃えてあるので、本文の組版と UI が同じ紙に載る。
//
// ペンは Badger と同じ 0（黒）〜15（白）の 16 段階。

#include <stdint.h>

#include <render/PageRenderer.h>

namespace pdos {

namespace fb = freeink::book;

// Badger の UPDATE_NORMAL / MEDIUM / FAST / TURBO に当たる。
//   Quality : 全面を点滅させて消す。写真向き（16 階調がきれいに出る）
//   Text    : 文字向き。変わったところだけ動く
//   Fast    : 白黒だけ。カーソル移動など。灰色はつぶして送る
//   Fastest : Fast より速いが残像が増える
enum class Update : uint8_t { Quality, Text, Fast, Fastest };

class Gfx {
 public:
  static constexpr int16_t W = 540;
  static constexpr int16_t H = 960;
  static constexpr int16_t kPanelW = 960;
  static constexpr int16_t kPanelH = 540;

  void begin();
  void setFonts(fb::FontChain* fonts) { fonts_ = fonts; }
  bool hasFonts() const { return fonts_ != nullptr; }
  fb::FontChain* fonts() { return fonts_; }

  // キャンバス本体（パネル座標、1 バイト 1 画素、0 = 黒）。
  uint8_t* canvas() { return canvas_; }
  fb::FrameTarget target() const;

  void setPen(uint8_t pen) { lum_ = static_cast<uint8_t>((pen > 15 ? 15 : pen) * 17); }
  void clear(uint8_t pen = 15);
  void pixel(int16_t x, int16_t y);
  void fill(int16_t x, int16_t y, int16_t w, int16_t h);
  void frame(int16_t x, int16_t y, int16_t w, int16_t h, int16_t thickness = 2);
  void hline(int16_t x, int16_t y, int16_t w, int16_t thickness = 1) { fill(x, y, w, thickness); }
  void invert(int16_t x, int16_t y, int16_t w, int16_t h);

  // 1bit の図柄（MSB 先頭、1 = インク）を今のペンで置く。scale は整数倍。
  void bitmap(const uint8_t* bits, int16_t w, int16_t h, int16_t x, int16_t y, int16_t scale = 1);

  // 8bit グレーの画像を置く（パネルへの回転はここで受ける）。
  void gray(const uint8_t* pixels, int16_t w, int16_t h, int16_t x, int16_t y);

  // 線と円。Python（Badger の line / circle）から使う。
  void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness = 1);
  void circle(int16_t cx, int16_t cy, int16_t r, bool filled = true);

  // UTF-8 を 1 行描く。y はベースライン、em は全角の大きさ（画素）。色は今のペン。
  // フォントが無いときは内蔵の英数字だけで描く。
  int16_t text(const char* s, int16_t x, int16_t y, int16_t em);
  int16_t textWidth(const char* s, int16_t em);
  // 字を縦に積む（見本や短い見出し用。本文の組版は JaBook が行う）。次の y を返す。
  int16_t textVertical(const char* s, int16_t x, int16_t y, int16_t em);
  void textCentered(const char* s, int16_t cx, int16_t y, int16_t em);
  void textRight(const char* s, int16_t right, int16_t y, int16_t em);
  // 幅 w で折り返して描き、次の行のベースラインを返す。
  // 行頭禁則の記号は前の行へぶら下げる。maxLines が 0 なら制限なし。
  int16_t textWrap(const char* s, int16_t x, int16_t y, int16_t w, int16_t em,
                   int16_t lineHeight = 0, int16_t maxLines = 0);

  // 全角の大きさから、フォントの指定サイズを逆算する。
  // stb のサイズ指定は ascent+descent 基準なので、そのままだと和文が小さくなる。
  uint16_t sizeForEm(int16_t em);

  // キャンバスをパネルへ送る。完了まで待つ。
  void update(Update mode);
  // 次のキーが待っているあいだ、Fast の送信をあとへ回す（連打で描画が溜まらないように）。
  // 描き手は毎回キャンバスを描き直すので、飛ばしても最後の絵は失われない。
  void setDeferFast(bool defer) { deferFast_ = defer; }
  // あとへ回したものがあれば送る。
  void flushDeferred();
  // すべての画素を白か黒へつぶす。Fast の前に呼ばれる。
  void quantize();
  uint32_t lastUpdateMs() const { return lastUpdateMs_; }

 private:
  uint8_t* at(int16_t x, int16_t y) const { return canvas_ + (kPanelH - 1 - x) * kPanelW + y; }
  void asciiChar(char c, int16_t x, int16_t y, int16_t em);

  uint8_t* canvas_ = nullptr;
  fb::FontChain* fonts_ = nullptr;
  uint8_t lum_ = 0;
  bool pushed_ = false;
  bool deferFast_ = false;
  bool deferred_ = false;
  uint32_t lastUpdateMs_ = 0;
  // 同じ大きさを何度も測らないように、直前の逆算結果を覚える。
  int16_t cachedEm_ = 0;
  uint16_t cachedSize_ = 0;
};

}  // namespace pdos
