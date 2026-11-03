// 青空文庫の記法を XHTML に直すところ。
// ハードにも SD カードにも触らないので、ホストのテストでそのまま回せる。

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Aozora.h"

namespace paperdiy {
namespace {

// UTF-8 を 1 文字読む。読んだバイト数を used に返す。
uint32_t utf8At(const char* s, size_t len, size_t at, size_t* used) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s) + at;
  const size_t left = len - at;
  if (left == 0) {
    *used = 0;
    return 0;
  }
  if (p[0] < 0x80) {
    *used = 1;
    return p[0];
  }
  if ((p[0] & 0xE0) == 0xC0 && left >= 2) {
    *used = 2;
    return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
  }
  if ((p[0] & 0xF0) == 0xE0 && left >= 3) {
    *used = 3;
    return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
  }
  if ((p[0] & 0xF8) == 0xF0 && left >= 4) {
    *used = 4;
    return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
  }
  *used = 1;
  return p[0];
}

// 1 文字ぶん前へ戻る（UTF-8 の先頭バイトまで）。
size_t utf8Back(const char* s, size_t at) {
  while (at > 0) {
    --at;
    if ((static_cast<uint8_t>(s[at]) & 0xC0) != 0x80) break;
  }
  return at;
}

bool isKanji(uint32_t c) {
  return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
         (c >= 0xF900 && c <= 0xFAFF) || c == 0x3005 /* 々 */;
}
bool isKatakana(uint32_t c) { return c >= 0x30A0 && c <= 0x30FF; }
bool isHiragana(uint32_t c) { return c >= 0x3040 && c <= 0x309F; }
bool isLatin(uint32_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xFF21 && c <= 0xFF3A) ||
         (c >= 0xFF41 && c <= 0xFF5A);
}

// ルビの親文字は「同じ種類の文字が続くところまで」。青空文庫の決まりに合わせる。
bool sameKind(uint32_t a, uint32_t b) {
  if (isKanji(a)) return isKanji(b);
  if (isKatakana(a)) return isKatakana(b);
  if (isHiragana(a)) return isHiragana(b);
  if (isLatin(a)) return isLatin(b);
  return false;
}

// 青空文庫でよく出る記号。
constexpr uint32_t kRubyOpen = 0x300A;   // 《
constexpr uint32_t kRubyClose = 0x300B;  // 》
constexpr uint32_t kRubyMark = 0xFF5C;   // ｜
constexpr uint32_t kNoteOpen = 0xFF3B;   // ［
constexpr uint32_t kNoteClose = 0xFF3D;  // ］
constexpr uint32_t kNoteHash = 0xFF03;   // ＃

struct Out {
  char* buf;
  size_t cap;
  size_t len;

  void put(const char* s, size_t n) {
    if (len + n + 1 > cap) n = (len + 1 < cap) ? cap - len - 1 : 0;
    if (n == 0) return;
    memcpy(buf + len, s, n);
    len += n;
  }
  void put(const char* s) { put(s, strlen(s)); }
  // XML に出せない文字だけ逃がす。
  void putEscaped(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      switch (s[i]) {
        case '&': put("&amp;"); break;
        case '<': put("&lt;"); break;
        case '>': put("&gt;"); break;
        default: put(s + i, 1); break;
      }
    }
  }
};

// 注記の中身が「見出し」を指しているか。
uint8_t headingLevelOf(const char* note, size_t len) {
  // ［＃「一」は大見出し］ のような形。
  const char* big = "大見出し";
  const char* mid = "中見出し";
  const char* small = "小見出し";
  const char* end = note + len;
  for (const char* p = note; p + 12 <= end; ++p) {
    if (strncmp(p, big, 12) == 0) return 1;
    if (strncmp(p, mid, 12) == 0) return 2;
    if (strncmp(p, small, 12) == 0) return 3;
  }
  return 0;
}

}  // namespace

bool parseAozoraImage(const char* line, char* nameOut, size_t nameCap, uint16_t* widthOut,
                      uint16_t* heightOut) {
  if (line == nullptr || nameOut == nullptr) return false;
  nameOut[0] = '\0';
  if (widthOut != nullptr) *widthOut = 0;
  if (heightOut != nullptr) *heightOut = 0;

  // ［＃…（fig42154_01.png、横119×縦207）入る］
  // 画像の名前は ASCII なので、そこを手がかりに探す。
  const char* dot = nullptr;
  for (const char* p = line; *p != '\0'; ++p) {
    if (strncasecmp(p, ".png", 4) == 0 || strncasecmp(p, ".jpg", 4) == 0 ||
        strncasecmp(p, ".gif", 4) == 0) {
      dot = p;
      break;
    }
  }
  if (dot == nullptr) return false;

  // 名前の先頭へ遡る（ASCII の英数字・記号が続くところまで）。
  const char* start = dot;
  while (start > line) {
    const char prev = *(start - 1);
    const bool ok = (prev >= '0' && prev <= '9') || (prev >= 'A' && prev <= 'Z') ||
                    (prev >= 'a' && prev <= 'z') || prev == '_' || prev == '-';
    if (!ok) break;
    --start;
  }
  const char* stop = dot + 4;
  const size_t nameLen = static_cast<size_t>(stop - start);
  if (nameLen == 0 || nameLen + 1 > nameCap) return false;
  memcpy(nameOut, start, nameLen);
  nameOut[nameLen] = '\0';

  // 「横119×縦207」から寸法を拾う。無いこともある。
  const char* yoko = strstr(stop, "横");
  if (yoko != nullptr && widthOut != nullptr) {
    *widthOut = static_cast<uint16_t>(strtoul(yoko + strlen("横"), nullptr, 10));
  }
  const char* tate = strstr(stop, "縦");
  if (tate != nullptr && heightOut != nullptr) {
    *heightOut = static_cast<uint16_t>(strtoul(tate + strlen("縦"), nullptr, 10));
  }
  return true;
}

size_t aozoraLineToXhtml(const char* line, char* out, size_t cap, uint8_t* headingOut) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (headingOut != nullptr) *headingOut = 0;
  if (line == nullptr) return 0;

  Out o{out, cap, 0};
  const size_t len = strlen(line);

  // ルビの親文字がどこから始まるかを覚えておく。書き出した位置で持つ。
  size_t plainStart = 0;  // 直前に書き出した「ふつうの文字」の始まり（出力側）
  size_t at = 0;

  while (at < len) {
    size_t used = 0;
    const uint32_t c = utf8At(line, len, at, &used);
    if (used == 0) break;

    // 注記 ［＃ … ］
    if (c == kNoteOpen) {
      size_t peek = at + used;
      size_t hashUsed = 0;
      const uint32_t next = utf8At(line, len, peek, &hashUsed);
      if (next == kNoteHash) {
        // 閉じ括弧まで飛ばす。中身は見出しかどうかだけ見る。
        size_t scan = peek + hashUsed;
        const size_t noteStart = scan;
        while (scan < len) {
          size_t u = 0;
          const uint32_t n = utf8At(line, len, scan, &u);
          if (u == 0) break;
          if (n == kNoteClose) break;
          scan += u;
        }
        if (headingOut != nullptr && *headingOut == 0) {
          const uint8_t level = headingLevelOf(line + noteStart, scan - noteStart);
          if (level != 0) *headingOut = level;
        }
        at = scan < len ? scan + 3 /* ］ は 3 バイト */ : len;
        plainStart = o.len;
        continue;
      }
    }

    // ｜ … 《ルビ》 — 親文字の始まりが明示されている形。
    if (c == kRubyMark) {
      const size_t baseStart = o.len;
      at += used;
      // 《 まで親文字。
      while (at < len) {
        size_t u = 0;
        const uint32_t n = utf8At(line, len, at, &u);
        if (u == 0 || n == kRubyOpen) break;
        o.putEscaped(line + at, u);
        at += u;
      }
      // ここから《ルビ》。
      if (at < len) {
        size_t u = 0;
        utf8At(line, len, at, &u);
        at += u;  // 《 を飛ばす
        char ruby[128];
        size_t rubyLen = 0;
        while (at < len) {
          size_t ru = 0;
          const uint32_t n = utf8At(line, len, at, &ru);
          if (ru == 0 || n == kRubyClose) break;
          if (rubyLen + ru < sizeof(ruby)) {
            memcpy(ruby + rubyLen, line + at, ru);
            rubyLen += ru;
          }
          at += ru;
        }
        if (at < len) at += 3;  // 》 を飛ばす
        ruby[rubyLen] = '\0';

        // いま書いた親文字を <ruby> で包み直す。
        char base[192];
        const size_t baseLen = o.len - baseStart;
        const size_t keep = baseLen < sizeof(base) - 1 ? baseLen : sizeof(base) - 1;
        memcpy(base, out + baseStart, keep);
        base[keep] = '\0';
        o.len = baseStart;
        o.put("<ruby>");
        o.put(base);
        o.put("<rt>");
        o.putEscaped(ruby, rubyLen);
        o.put("</rt></ruby>");
      }
      plainStart = o.len;
      continue;
    }

    // 漢字《かんじ》 — 親文字の範囲は、同じ種類の文字が続くところまで遡る。
    if (c == kRubyOpen) {
      // 出力の末尾から、同じ種類の文字を遡る。
      size_t back = o.len;
      uint32_t kind = 0;
      while (back > plainStart) {
        const size_t prev = utf8Back(out, back);
        size_t u = 0;
        const uint32_t pc = utf8At(out, o.len, prev, &u);
        if (kind == 0) {
          if (!isKanji(pc) && !isKatakana(pc) && !isHiragana(pc) && !isLatin(pc)) break;
          kind = pc;
        } else if (!sameKind(kind, pc)) {
          break;
        }
        back = prev;
      }

      at += used;  // 《 を飛ばす
      char ruby[128];
      size_t rubyLen = 0;
      while (at < len) {
        size_t ru = 0;
        const uint32_t n = utf8At(line, len, at, &ru);
        if (ru == 0 || n == kRubyClose) break;
        if (rubyLen + ru < sizeof(ruby)) {
          memcpy(ruby + rubyLen, line + at, ru);
          rubyLen += ru;
        }
        at += ru;
      }
      if (at < len) at += 3;  // 》
      ruby[rubyLen] = '\0';

      if (back < o.len) {
        char base[192];
        const size_t baseLen = o.len - back;
        const size_t keep = baseLen < sizeof(base) - 1 ? baseLen : sizeof(base) - 1;
        memcpy(base, out + back, keep);
        base[keep] = '\0';
        o.len = back;
        o.put("<ruby>");
        o.put(base);
        o.put("<rt>");
        o.putEscaped(ruby, rubyLen);
        o.put("</rt></ruby>");
      }
      plainStart = o.len;
      continue;
    }

    o.putEscaped(line + at, used);
    at += used;
  }

  out[o.len] = '\0';
  return o.len;
}

}  // namespace paperdiy
