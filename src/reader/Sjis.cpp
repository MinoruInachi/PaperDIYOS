#include "Sjis.h"

#include "SjisTable.h"

namespace paperdiy {
namespace {

constexpr uint32_t kReplacement = 0xFFFD;  // 読めなかった印（下駄）

// 2 バイト文字の先頭バイトか。
bool isLead(uint8_t b) { return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC); }

// 先頭バイトを、表の詰めた番号に直す。
int16_t leadIndex(uint8_t b) {
  if (b >= 0x81 && b <= 0x9F) return static_cast<int16_t>(b - 0x81);
  if (b >= 0xE0 && b <= 0xFC) return static_cast<int16_t>((0x9F - 0x81 + 1) + (b - 0xE0));
  return -1;
}

}  // namespace

uint32_t sjisNext(const uint8_t* src, size_t len, size_t* used) {
  if (src == nullptr || len == 0) {
    if (used != nullptr) *used = 0;
    return 0;
  }

  const uint8_t first = src[0];

  // ASCII。ただし Shift_JIS では 0x5C が円記号、0x7E がオーバーラインだが、
  // 青空文庫のテキストでは ASCII として扱うほうが素直なのでそのまま通す。
  if (first < 0x80) {
    if (used != nullptr) *used = 1;
    return first;
  }

  // 半角カナ。U+FF61 から並んでいるので計算で出せる。
  if (first >= 0xA1 && first <= 0xDF) {
    if (used != nullptr) *used = 1;
    return 0xFF61u + (first - 0xA1);
  }

  if (isLead(first) && len >= 2) {
    const uint8_t second = src[1];
    if (second >= kSjisTrailLo && second <= kSjisTrailHi && second != 0x7F) {
      const int16_t lead = leadIndex(first);
      if (lead >= 0) {
        const uint16_t code =
            kSjisToUnicode[static_cast<size_t>(lead) * kSjisTrails + (second - kSjisTrailLo)];
        if (code != 0) {
          if (used != nullptr) *used = 2;
          return code;
        }
      }
    }
    // 表に無い組み合わせ（外字など）。2 バイトまとめて下駄にする。
    if (used != nullptr) *used = 2;
    return kReplacement;
  }

  if (used != nullptr) *used = 1;
  return kReplacement;
}

size_t utf8Encode(uint32_t code, char* out, size_t cap) {
  if (out == nullptr) return 0;
  if (code < 0x80) {
    if (cap < 1) return 0;
    out[0] = static_cast<char>(code);
    return 1;
  }
  if (code < 0x800) {
    if (cap < 2) return 0;
    out[0] = static_cast<char>(0xC0 | (code >> 6));
    out[1] = static_cast<char>(0x80 | (code & 0x3F));
    return 2;
  }
  if (code < 0x10000) {
    if (cap < 3) return 0;
    out[0] = static_cast<char>(0xE0 | (code >> 12));
    out[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (code & 0x3F));
    return 3;
  }
  if (cap < 4) return 0;
  out[0] = static_cast<char>(0xF0 | (code >> 18));
  out[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (code & 0x3F));
  return 4;
}

size_t sjisToUtf8(const uint8_t* src, size_t len, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  size_t written = 0;
  size_t at = 0;
  while (at < len) {
    size_t used = 0;
    const uint32_t code = sjisNext(src + at, len - at, &used);
    if (used == 0) break;
    at += used;
    // 終端のぶんを 1 バイト残す。入らなければ、その文字は書かずに終える。
    const size_t got = utf8Encode(code, out + written, cap - written - 1);
    if (got == 0) break;
    written += got;
  }
  out[written] = '\0';
  return written;
}

}  // namespace paperdiy
