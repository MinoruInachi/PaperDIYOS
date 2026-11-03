#include "Keyboard.h"

#include <stdio.h>
#include <string.h>

#include "Gfx.h"
#include "I18n.h"
#include "Os.h"
#include "Ui.h"

namespace pdos {
namespace ui {
namespace {

constexpr int16_t kCols = 10;
constexpr int16_t kCharRows = 4;
constexpr int16_t kRows = kCharRows + 1;  // 最下段は操作キー
constexpr int16_t kActions = 5;           // 切替 / 空白 / 消す / 見る / 決定
constexpr int16_t kKeyH = 88;
constexpr int16_t kTop = 340;
constexpr int16_t kKeyW = Gfx::W / kCols;

constexpr const char kLayouts[3][kCharRows][kCols + 1] = {
    {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"},
    {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.@"},
    {"!#$%&'()*+", ",/:;<=>?[]", "\\^`{|}~\"  ", "          "},
};

}  // namespace

void Keyboard::open(const char* title, const char* initial, size_t maxLen, bool secret) {
  title_ = title;
  maxLen_ = maxLen < sizeof(text_) - 1 ? maxLen : sizeof(text_) - 1;
  snprintf(text_, sizeof(text_), "%s", initial != nullptr ? initial : "");
  secret_ = secret;
  revealed_ = false;
  mode_ = Lower;
  row_ = 1;
  col_ = 0;
}

char Keyboard::charAt(int16_t row, int16_t col) const { return kLayouts[mode_][row][col]; }

void Keyboard::press() {
  if (row_ < kCharRows) {
    const char c = charAt(row_, col_);
    const size_t len = strlen(text_);
    if (c != ' ' && len < maxLen_) {
      text_[len] = c;
      text_[len + 1] = '\0';
    }
    return;
  }
  switch (col_) {
    case 0:  // 切替
      mode_ = static_cast<Mode>((mode_ + 1) % ModeCount);
      break;
    case 1: {  // 空白
      const size_t len = strlen(text_);
      if (len < maxLen_) {
        text_[len] = ' ';
        text_[len + 1] = '\0';
      }
      break;
    }
    case 2: {  // 消す
      const size_t len = strlen(text_);
      if (len > 0) text_[len - 1] = '\0';
      break;
    }
    case 3:  // 見る
      revealed_ = !revealed_;
      break;
    default:
      break;
  }
}

Keyboard::Result Keyboard::key(Key k) {
  switch (k) {
    case Key::A:
      if (row_ < kCharRows) {
        col_ = static_cast<int16_t>((col_ + 1) % kCols);
      } else {
        col_ = static_cast<int16_t>((col_ + 1) % kActions);
      }
      break;
    case Key::B: {
      const int16_t next = static_cast<int16_t>((row_ + 1) % kRows);
      // 文字の段と操作の段は列の数が違うので、位置を置きかえる。
      if (next == kCharRows) col_ = static_cast<int16_t>(col_ * kActions / kCols);
      if (row_ == kCharRows) col_ = static_cast<int16_t>(col_ * kCols / kActions);
      row_ = next;
      break;
    }
    case Key::LongB:
      if (row_ == kCharRows && col_ == kActions - 1) return Result::Done;
      press();
      break;
    case Key::LongA: {
      const size_t len = strlen(text_);
      if (len == 0) return Result::Cancelled;
      text_[len - 1] = '\0';
      break;
    }
    default:
      break;
  }
  draw();
  return Result::Editing;
}

void Keyboard::drawKeyCap(int16_t row, int16_t col, const char* label, int16_t x, int16_t y, int16_t w, int16_t h) {
  Gfx& g = os.gfx();
  g.setPen(0);
  g.frame(x + 2, y + 2, w - 4, h - 4, 1);
  int16_t em = 34;
  while (em > 16 && g.textWidth(label, em) > w - 8) em -= 2;
  g.textCentered(label, x + w / 2, y + h / 2 + em * 2 / 5, em);
  if (row == row_ && col == col_) g.invert(x + 2, y + 2, w - 4, h - 4);
}

void Keyboard::draw() {
  Gfx& g = os.gfx();
  g.clear();
  header(title_);

  // 入力欄。パスワードは最後の 1 字だけ見せる。
  char shown[sizeof(text_)];
  snprintf(shown, sizeof(shown), "%s", text_);
  const size_t len = strlen(text_);
  if (secret_ && !revealed_ && len > 0) {
    memset(shown, '*', len - 1);
  }
  g.setPen(0);
  g.frame(kMargin, kHeaderH + 30, Gfx::W - kMargin * 2, 92, 2);
  // 長い文字は末尾が見えるように頭を落とす。
  const char* tail = shown;
  while (*tail != '\0' && g.textWidth(tail, 28) > Gfx::W - kMargin * 2 - 32) ++tail;
  g.text(tail, kMargin + 16, kHeaderH + 90, 28);

  g.setPen(0);
  g.textWrap(TR("A:右へ  B:下へ  B長押し:押す  A長押し:1字消す（空なら戻る）",
               "A:Right  B:Down  Hold B:Press  Hold A:Delete (back if empty)"),
             kMargin, 240, Gfx::W - kMargin * 2, 20, 32);

  char label[2] = {0, 0};
  for (int16_t r = 0; r < kCharRows; ++r) {
    for (int16_t c = 0; c < kCols; ++c) {
      label[0] = charAt(r, c);
      drawKeyCap(r, c, label, c * kKeyW, kTop + r * kKeyH, kKeyW, kKeyH);
    }
  }
  const char* const actions[kActions] = {
      mode_ == Lower ? "ABC" : mode_ == Upper ? "#+=" : "abc",
      TR("空白", "Space"),
      TR("消す", "Del"),
      TR("見る", "Show"),
      TR("決定", "OK"),
  };
  const int16_t w = Gfx::W / kActions;
  for (int16_t c = 0; c < kActions; ++c) {
    drawKeyCap(kCharRows, c, actions[c], c * w, kTop + kCharRows * kKeyH, w, kKeyH);
  }
  g.update(Update::Fast);
}

}  // namespace ui
}  // namespace pdos
