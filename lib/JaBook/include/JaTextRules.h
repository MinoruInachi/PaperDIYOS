#pragma once

// JaBook — 日本語組版ルール（縦書き字形分類・禁則処理）
//
// FreeInkBook の横組みエンジンは UAX #14 と JLREQ の約物処理を持つが、
// 縦組み固有の字形回転・約物位置・縦中横は扱わない。このヘッダはその
// 差分だけを担う純粋関数群で、Arduino / ESP-IDF に依存しない
// freestanding C++17。ホストテストからそのまま呼べる。

#include <stdint.h>

namespace jabook {

// 縦組みでの一文字の扱い。
enum class VertForm : uint8_t {
  // そのままの字形で縦に積む（漢字・かな・全角記号の大半）。
  Upright,
  // 時計回り 90 度回転して積む（括弧・長音符・ダッシュ・三点リーダ・欧文）。
  // 横組み字形を回した形が、そのまま正しい縦組み字形になる字だけを選ぶ。
  Rotated,
  // 字形は回さず、全角枠の右上へ寄せる（、。，．）。
  PunctTopRight,
  // 字形は回さず、わずかに右上へ寄せる（小書きのかな）。
  SmallKana,
};

VertForm vertFormOf(uint32_t cp);

// 行頭に置いてはいけない字（終わり括弧・句読点・繰り返し記号など）。
// strict = true で JLREQ の厳格版（小書きのかな・長音符も行頭禁則）。
bool isLineStartProhibited(uint32_t cp, bool strict);

// 行末に置いてはいけない字（始め括弧）。
bool isLineEndProhibited(uint32_t cp);

// 分離禁止（—— …… ‥‥ など、同じ字が連続したとき間で切らない）。
bool isInseparable(uint32_t cp);

// 縦中横の候補になる半角文字（ASCII 数字）。
bool isAsciiDigit(uint32_t cp);

// 回転して組む欧文（ラテン文字・ASCII 記号）。連続する語はひとまとまりで倒す。
bool isLatinRun(uint32_t cp);

// 空白として畳む文字。
bool isSpace(uint32_t cp);

// 和文（全角として送る）かどうか。
bool isFullWidth(uint32_t cp);

// --- UTF-8 ------------------------------------------------------------------

// `s[i]` から 1 コードポイント読み、`i` を進める。不正バイトは U+FFFD 扱い。
uint32_t utf8Next(const char* s, uint32_t len, uint32_t* i);

// コードポイントを UTF-8 で書き出す。書いたバイト数（最大 4）を返す。
uint32_t utf8Encode(uint32_t cp, char* out);

}  // namespace jabook
