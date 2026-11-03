#include "PathUtil.h"

#include <string.h>

namespace paperdiy {

uint32_t pathHash(const char* path) {
  uint32_t h = 0x811C9DC5u;
  for (; *path != '\0'; ++path) {
    h ^= static_cast<uint8_t>(*path);
    h *= 16777619u;
  }
  return h;
}

namespace {

bool isForbidden(unsigned char c) {
  if (c < 0x20) return true;  // 制御文字
  switch (c) {
    case '\\':
    case '/':
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
    case ' ':
      return true;
    default:
      return false;
  }
}

}  // namespace

void trimUtf8(char* s) {
  if (s == nullptr) return;
  const size_t len = strlen(s);
  if (len == 0) return;

  // 末尾の文字の先頭バイトまで遡る。継続バイト（10xxxxxx）は最大 3 つ。
  size_t start = len;
  for (size_t back = 0; start > 0 && back < 4; ++back) {
    --start;
    if ((static_cast<unsigned char>(s[start]) & 0xC0) != 0x80) break;
  }

  // その先頭バイトが要求するバイト数と、実際に残っている数を比べる。
  // 足りていれば完全な文字なので、何も削らない。
  const unsigned char lead = static_cast<unsigned char>(s[start]);
  size_t need = 0;
  if ((lead & 0x80) == 0x00) {
    need = 1;
  } else if ((lead & 0xE0) == 0xC0) {
    need = 2;
  } else if ((lead & 0xF0) == 0xE0) {
    need = 3;
  } else if ((lead & 0xF8) == 0xF0) {
    need = 4;
  } else {
    s[start] = '\0';  // 継続バイトしか残っていない
    return;
  }
  if (len - start < need) s[start] = '\0';
}

void sanitizeFatName(const char* in, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (in == nullptr) return;

  size_t used = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in);
       *p != '\0' && used + 1 < cap; ++p) {
    out[used++] = isForbidden(*p) ? '-' : static_cast<char>(*p);
  }
  out[used] = '\0';
  trimUtf8(out);
}

}  // namespace paperdiy
