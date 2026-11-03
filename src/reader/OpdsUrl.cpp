// URL のつなぎ方と、そこから作るファイル名。
// ハードにも SD カードにも触らないので、ホストのテストでそのまま回せる。

#include <stdio.h>
#include <string.h>

#include "Opds.h"
#include "PathUtil.h"

namespace paperdiy {
namespace {

bool contains(const char* haystack, const char* needle) {
  return haystack != nullptr && strstr(haystack, needle) != nullptr;
}

bool isAbsolute(const char* url) {
  return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

// "https://host/a/b" の "https://host" までの長さ。
size_t originLength(const char* url) {
  const char* slashes = strstr(url, "//");
  if (slashes == nullptr) return 0;
  const char* slash = strchr(slashes + 2, '/');
  return slash != nullptr ? static_cast<size_t>(slash - url) : strlen(url);
}

// 末尾の拡張子を見る。URL には ?size=1 のようなクエリが付くことがある。
bool hasExtension(const char* url, const char* ext) {
  if (url == nullptr) return false;
  const char* end = strpbrk(url, "?#");
  const size_t len = end != nullptr ? static_cast<size_t>(end - url) : strlen(url);
  const size_t extLen = strlen(ext);
  return len > extLen && strncasecmp(url + len - extLen, ext, extLen) == 0;
}

}  // namespace

uint8_t acquisitionRank(const char* rel, const char* type, const char* href) {
  // rel を書かないサーバもあるので、型か拡張子で本と分かるものも拾う。
  const bool acquisition = contains(rel, "acquisition") || contains(type, "epub+zip") ||
                           hasExtension(href, ".epub") || hasExtension(href, ".txt");
  if (!acquisition) return 0;

  // EPUB の型は "application/epub+zip"。ただの zip と混ざらないよう先に見る。
  if (contains(type, "epub+zip") || hasExtension(href, ".epub")) return 4;
  if (contains(type, "text/plain") || hasExtension(href, ".txt")) return 3;
  // ZIP は青空文庫のテキスト形式かもしれない。落としてから中を見て、
  // テキストが入っていれば EPUB に組み直す。
  if (contains(type, "application/zip") || hasExtension(href, ".zip")) return 2;
  // 入手リンクではあるが、この端末では開けない形式（PDF、xhtml など）。
  return 1;
}

void resolveUrl(const char* base, const char* href, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (href == nullptr || href[0] == '\0') return;

  if (isAbsolute(href)) {
    snprintf(out, cap, "%s", href);
    return;
  }
  if (base == nullptr || !isAbsolute(base)) {
    snprintf(out, cap, "%s", href);
    return;
  }

  if (href[0] == '/') {
    // 同じサーバの、根からの道。
    const size_t origin = originLength(base);
    snprintf(out, cap, "%.*s%s", static_cast<int>(origin), base, href);
    return;
  }

  // 相対。いま見ているものの「ひとつ上」にくっつける。
  char dir[512];
  snprintf(dir, sizeof(dir), "%s", base);
  // クエリと断片は落とす。
  char* cut = strpbrk(dir, "?#");
  if (cut != nullptr) *cut = '\0';
  char* slash = strrchr(dir, '/');
  const size_t origin = originLength(base);
  if (slash != nullptr && static_cast<size_t>(slash - dir) >= origin) {
    *(slash + 1) = '\0';
  } else {
    snprintf(dir, sizeof(dir), "%.*s/", static_cast<int>(origin), base);
  }
  snprintf(out, cap, "%s%s", dir, href);
}

void filenameFromUrl(const char* url, const char* fallback, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (url == nullptr) return;

  // クエリと断片を落としてから、最後の / の後ろを取る。
  char work[512];
  snprintf(work, sizeof(work), "%s", url);
  char* cut = strpbrk(work, "?#");
  if (cut != nullptr) *cut = '\0';

  const char* slash = strrchr(work, '/');
  const char* name = slash != nullptr ? slash + 1 : work;

  // URL の末尾に拡張子が付いているときだけ、それを名前として使う。
  // OPDS の配り口は "/get/EPUB/123" のように拡張子を持たないことが多く、
  // そのまま使うとどの本も同じ名前になってしまう。そういうときは題を使う。
  //
  // .zip も残す。青空文庫のテキスト形式で、落としたあとに EPUB へ組み直す。
  // ここで .epub に化けさせてしまうと、中身が ZIP のまま開けなくなる。
  const size_t nameLen = strlen(name);
  const bool named = (nameLen > 5 && strcasecmp(name + nameLen - 5, ".epub") == 0) ||
                     (nameLen > 4 && strcasecmp(name + nameLen - 4, ".txt") == 0) ||
                     (nameLen > 4 && strcasecmp(name + nameLen - 4, ".zip") == 0);

  char safe[128];
  if (named) {
    sanitizeFatName(name, safe, sizeof(safe));
    if (safe[0] != '\0') {
      snprintf(out, cap, "%s", safe);
      trimUtf8(out);
      return;
    }
  }

  sanitizeFatName(fallback != nullptr ? fallback : "book", safe, sizeof(safe));
  if (safe[0] == '\0') snprintf(safe, sizeof(safe), "book");
  snprintf(out, cap, "%s.epub", safe);
  trimUtf8(out);
}

}  // namespace paperdiy
