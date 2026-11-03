#include "JaTextRules.h"

namespace jabook {
namespace {

// 表は昇順のコードポイント範囲。二分探索する。
struct Range {
  uint32_t lo;
  uint32_t hi;
};

bool inRanges(const Range* table, uint32_t count, uint32_t cp) {
  uint32_t lo = 0, hi = count;
  while (lo < hi) {
    const uint32_t mid = (lo + hi) / 2;
    if (cp < table[mid].lo) {
      hi = mid;
    } else if (cp > table[mid].hi) {
      lo = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}

// 時計回りに 90 度回した形が正しい縦組み字形になる字。
// 括弧・罫線・長音符・ダッシュ・リーダ、そしてラテン文字全般。
constexpr Range kRotated[] = {
    {0x0021, 0x007E},  // ASCII 図形文字すべて（欧文はまとめて倒す）
    {0x00A1, 0x024F},  // ラテン 1 補助・拡張
    {0x2013, 0x2015},  // – — ―
    {0x2025, 0x2026},  // ‥ …
    {0x2032, 0x2033},  // ′ ″
    {0x2190, 0x2193},  // ← ↑ → ↓
    {0x2500, 0x2501},  // ─ ━
    {0x3008, 0x3011},  // 〈〉《》「」『』【】
    {0x3014, 0x301C},  // 〔〕〖〗〘〙〚〛〜
    {0x30FC, 0x30FC},  // ー
    {0xFF08, 0xFF09},  // （）
    {0xFF0D, 0xFF0D},  // －
    {0xFF3B, 0xFF3B},  // ［
    {0xFF3D, 0xFF3D},  // ］
    {0xFF5B, 0xFF5B},  // ｛
    {0xFF5D, 0xFF5E},  // ｝～
    {0xFF62, 0xFF63},  // ｢｣
};

// 全角枠の右上に寄せる約物。
constexpr Range kPunctTopRight[] = {
    {0x3001, 0x3002},  // 、。
    {0xFF0C, 0xFF0C},  // ，
    {0xFF0E, 0xFF0E},  // ．
};

// 小書きのかな。
constexpr Range kSmallKana[] = {
    {0x3041, 0x3041}, {0x3043, 0x3043}, {0x3045, 0x3045}, {0x3047, 0x3047},
    {0x3049, 0x3049}, {0x3063, 0x3063}, {0x3083, 0x3083}, {0x3085, 0x3085},
    {0x3087, 0x3087}, {0x308E, 0x308E}, {0x3095, 0x3096}, {0x30A1, 0x30A1},
    {0x30A3, 0x30A3}, {0x30A5, 0x30A5}, {0x30A7, 0x30A7}, {0x30A9, 0x30A9},
    {0x30C3, 0x30C3}, {0x30E3, 0x30E3}, {0x30E5, 0x30E5}, {0x30E7, 0x30E7},
    {0x30EE, 0x30EE}, {0x30F5, 0x30F6},
};

// 行頭禁則（終わり括弧・句読点・中点・繰り返し記号・感嘆符）。
constexpr Range kStartProhibited[] = {
    {0x0021, 0x0021},  // !
    {0x0029, 0x0029},  // )
    {0x002C, 0x002C},  // ,
    {0x002E, 0x002E},  // .
    {0x003A, 0x003B},  // : ;
    {0x003F, 0x003F},  // ?
    {0x005D, 0x005D},  // ]
    {0x007D, 0x007D},  // }
    {0x2019, 0x2019},  // ’
    {0x201D, 0x201D},  // ”
    {0x2025, 0x2026},  // ‥ …
    {0x3001, 0x3002},  // 、。
    {0x3005, 0x3005},  // 々
    {0x3009, 0x3009},  // 〉
    {0x300B, 0x300B},  // 》
    {0x300D, 0x300D},  // 」
    {0x300F, 0x300F},  // 』
    {0x3011, 0x3011},  // 】
    {0x3015, 0x3015},  // 〕
    {0x3017, 0x3017},  // 〗
    {0x3019, 0x3019},  // 〙
    {0x301B, 0x301B},  // 〛
    {0x301E, 0x301F},  // 〞〟
    {0x303B, 0x303B},  // 〻
    {0x309D, 0x309E},  // ゝゞ
    {0x30FD, 0x30FE},  // ヽヾ
    {0xFF01, 0xFF01},  // ！
    {0xFF09, 0xFF09},  // ）
    {0xFF0C, 0xFF0C},  // ，
    {0xFF0E, 0xFF0E},  // ．
    {0xFF1A, 0xFF1B},  // ：；
    {0xFF1F, 0xFF1F},  // ？
    {0xFF3D, 0xFF3D},  // ］
    {0xFF5D, 0xFF5D},  // ｝
    {0xFF61, 0xFF61},  // ｡
    {0xFF63, 0xFF64},  // ｣､
};

// 行末禁則（始め括弧）。
constexpr Range kEndProhibited[] = {
    {0x0028, 0x0028},  // (
    {0x005B, 0x005B},  // [
    {0x007B, 0x007B},  // {
    {0x2018, 0x2018},  // ‘
    {0x201C, 0x201C},  // “
    {0x3008, 0x3008},  // 〈
    {0x300A, 0x300A},  // 《
    {0x300C, 0x300C},  // 「
    {0x300E, 0x300E},  // 『
    {0x3010, 0x3010},  // 【
    {0x3014, 0x3014},  // 〔
    {0x3016, 0x3016},  // 〖
    {0x3018, 0x3018},  // 〘
    {0x301A, 0x301A},  // 〚
    {0x301D, 0x301D},  // 〝
    {0xFF08, 0xFF08},  // （
    {0xFF3B, 0xFF3B},  // ［
    {0xFF5B, 0xFF5B},  // ｛
    {0xFF62, 0xFF62},  // ｢
};

// 同じ字が連なったとき、その間で行を割らない字。
constexpr Range kInseparable[] = {
    {0x2014, 0x2015},  // — ―
    {0x2025, 0x2026},  // ‥ …
    {0x3033, 0x3035},  // 〳〴〵
};

template <uint32_t N>
constexpr uint32_t countOf(const Range (&)[N]) {
  return N;
}

}  // namespace

VertForm vertFormOf(uint32_t cp) {
  if (inRanges(kPunctTopRight, countOf(kPunctTopRight), cp)) return VertForm::PunctTopRight;
  if (inRanges(kSmallKana, countOf(kSmallKana), cp)) return VertForm::SmallKana;
  if (inRanges(kRotated, countOf(kRotated), cp)) return VertForm::Rotated;
  return VertForm::Upright;
}

bool isLineStartProhibited(uint32_t cp, bool strict) {
  if (inRanges(kStartProhibited, countOf(kStartProhibited), cp)) return true;
  if (!strict) return false;
  if (cp == 0x30FC) return true;  // ー
  return inRanges(kSmallKana, countOf(kSmallKana), cp);
}

bool isLineEndProhibited(uint32_t cp) {
  return inRanges(kEndProhibited, countOf(kEndProhibited), cp);
}

bool isInseparable(uint32_t cp) { return inRanges(kInseparable, countOf(kInseparable), cp); }

bool isAsciiDigit(uint32_t cp) { return cp >= '0' && cp <= '9'; }

bool isLatinRun(uint32_t cp) {
  if (cp >= 0x21 && cp <= 0x7E) return true;
  return cp >= 0x00C0 && cp <= 0x024F;
}

bool isSpace(uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x00A0 || cp == 0x3000;
}

bool isFullWidth(uint32_t cp) {
  if (cp < 0x1100) return false;
  if (cp >= 0x2E80 && cp <= 0x303E) return true;   // CJK 部首・記号
  if (cp >= 0x3041 && cp <= 0x33FF) return true;   // かな・記号
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;   // 拡張 A
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;   // 統合漢字
  if (cp >= 0xF900 && cp <= 0xFAFF) return true;   // 互換漢字
  if (cp >= 0xFE10 && cp <= 0xFE19) return true;   // 縦組み約物
  if (cp >= 0xFE30 && cp <= 0xFE4F) return true;   // 縦組み括弧
  if (cp >= 0xFF01 && cp <= 0xFF60) return true;   // 全角英数・記号
  if (cp >= 0xFFE0 && cp <= 0xFFE6) return true;   // 全角通貨
  if (cp >= 0x20000 && cp <= 0x3FFFD) return true; // 拡張 B 以降
  return false;
}

uint32_t utf8Next(const char* s, uint32_t len, uint32_t* i) {
  if (*i >= len) return 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
  const uint8_t b0 = p[*i];
  if (b0 < 0x80) {
    ++*i;
    return b0;
  }
  uint32_t need;
  uint32_t cp;
  if ((b0 & 0xE0) == 0xC0) {
    need = 1;
    cp = b0 & 0x1Fu;
  } else if ((b0 & 0xF0) == 0xE0) {
    need = 2;
    cp = b0 & 0x0Fu;
  } else if ((b0 & 0xF8) == 0xF0) {
    need = 3;
    cp = b0 & 0x07u;
  } else {
    ++*i;
    return 0xFFFD;
  }
  if (*i + need >= len) {  // 末尾で切れている
    *i = len;
    return 0xFFFD;
  }
  for (uint32_t k = 1; k <= need; ++k) {
    const uint8_t b = p[*i + k];
    if ((b & 0xC0) != 0x80) {
      ++*i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (b & 0x3Fu);
  }
  *i += need + 1;
  return cp;
}

uint32_t utf8Encode(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

}  // namespace jabook
