#pragma once

// アプリで共通に使う画面の部品。見出し、一覧、案内の行。

#include <stdint.h>

#include "Gfx.h"

namespace pdos {
namespace ui {

inline constexpr int16_t kHeaderH = 52;
inline constexpr int16_t kFooterH = 44;
inline constexpr int16_t kMargin = 24;

// 上端の黒帯。左に見出し、右に電池の残量。
void header(const char* title);
// 下端の案内。ボタンの割り当てを書く。
void footer(const char* hints);
// 画面の中央に短い知らせ（Text で送る）。
void message(const char* line1, const char* line2 = nullptr);

// QR コードを描く。size は一辺の画素数の目安（モジュールの整数倍に丸める）。
// 実際に描いた一辺を返す。作れなければ 0。
int16_t qr(const char* text, int16_t x, int16_t y, int16_t size);

// 画像ファイル（JPEG / PNG / BMP）を 8bit グレーの箱へ縮めて描く。Gfx::gray と
// 組で使う。w x h のキャンバスを PSRAM に作り、中央へ合わせて置く。
// 読めなければ nullptr。戻り値は heap_caps_free で返す。
uint8_t* decodeImage(const char* path, int16_t w, int16_t h);
// ヘッダだけ読んで画像の大きさを返す。
bool imageSize(const char* path, int16_t* w, int16_t* h);

// 2 ボタンで動かす一覧。A で上、B で下、選択は OS の呼び出し側が B 長押しで受ける。
class ListView {
 public:
  void reset(uint16_t count, int16_t selected = 0);
  void setCount(uint16_t count);
  int16_t selected() const { return selected_; }
  uint16_t count() const { return count_; }
  void select(int16_t index);
  void up();
  void down();

  // label(i, buf, cap) で行の文字を受け取り、見出しと案内のあいだに描く。
  using LabelFn = void (*)(uint16_t index, char* buf, size_t cap, void* user);
  void draw(LabelFn label, void* user, int16_t rowH = 64, int16_t em = 26);

 private:
  uint16_t count_ = 0;
  int16_t selected_ = 0;
  int16_t top_ = 0;
};

}  // namespace ui
}  // namespace pdos
