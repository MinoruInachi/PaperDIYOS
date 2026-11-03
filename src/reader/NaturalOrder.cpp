#include "NaturalOrder.h"

#include <stddef.h>

namespace paperdiy {
namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

bool naturalLess(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  while (*a != '\0' && *b != '\0') {
    if (isDigit(*a) && isDigit(*b)) {
      // 数字が続くあいだはひとまとまりの数として比べる。
      while (*a == '0' && isDigit(a[1])) ++a;  // 先頭のゼロは数えない
      while (*b == '0' && isDigit(b[1])) ++b;
      const char* da = a;
      const char* db = b;
      while (isDigit(*da)) ++da;
      while (isDigit(*db)) ++db;
      const ptrdiff_t la = da - a, lb = db - b;
      if (la != lb) return la < lb;  // 桁数が違えば少ないほうが小さい
      while (a < da) {
        if (*a != *b) return *a < *b;
        ++a;
        ++b;
      }
      continue;
    }
    const char ca = lower(*a), cb = lower(*b);
    if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb);
    ++a;
    ++b;
  }
  return *a == '\0' && *b != '\0';
}

}  // namespace paperdiy
