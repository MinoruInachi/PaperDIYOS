#pragma once

// 2 ボタンの文字入力。Wi-Fi のパスワードと NTP サーバーの入力に使う。
//
//   A を押す      右へ（行の端で折り返す）
//   B を押す      下へ（列の端で折り返す）
//   B を長く押す  カーソルのキーを押す
//   A を長く押す  1 字消す。空なら取り消して閉じる
//
// キーの配置は 10 列 4 段の文字と、最下段の操作キー。

#include <stddef.h>
#include <stdint.h>

#include "Buttons.h"

namespace pdos {
namespace ui {

class Keyboard {
 public:
  enum class Result : uint8_t { Editing, Done, Cancelled };

  // secret なら入力欄は最後の 1 字以外を伏せる。
  void open(const char* title, const char* initial, size_t maxLen, bool secret);
  Result key(Key k);
  void draw();
  const char* text() const { return text_; }

 private:
  enum Mode : uint8_t { Lower, Upper, Symbol, ModeCount };
  void press();
  char charAt(int16_t row, int16_t col) const;
  void drawKeyCap(int16_t row, int16_t col, const char* label, int16_t x, int16_t y, int16_t w, int16_t h);

  const char* title_ = "";
  char text_[80] = {0};
  size_t maxLen_ = 0;
  bool secret_ = false;
  bool revealed_ = false;
  Mode mode_ = Lower;
  int16_t row_ = 0;
  int16_t col_ = 0;
};

}  // namespace ui
}  // namespace pdos
