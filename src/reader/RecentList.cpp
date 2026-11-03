// 「最近読んだ本」の並びの操作。SD カードには触らないので、
// ホストのテストでそのまま回せる。

#include <stdio.h>
#include <string.h>

#include "RecentBooks.h"

namespace paperdiy {

int16_t recentFind(const RecentBook* items, uint8_t count, const char* path) {
  if (items == nullptr || path == nullptr) return -1;
  for (uint8_t i = 0; i < count; ++i) {
    if (strcmp(items[i].path, path) == 0) return static_cast<int16_t>(i);
  }
  return -1;
}

void recentRemoveAt(RecentBook* items, uint8_t* count, uint8_t index) {
  if (items == nullptr || count == nullptr || index >= *count) return;
  for (uint8_t i = index; i + 1 < *count; ++i) items[i] = items[i + 1];
  --(*count);
}

void recentInsert(RecentBook* items, uint8_t* count, uint8_t capacity, const char* path,
                  const char* title) {
  if (items == nullptr || count == nullptr || capacity == 0 || path == nullptr) return;

  // すでに入っている本は、いったん抜いてから先頭に入れ直す。
  const int16_t existing = recentFind(items, *count, path);
  if (existing >= 0) recentRemoveAt(items, count, static_cast<uint8_t>(existing));

  if (*count < capacity) ++(*count);
  for (uint8_t i = static_cast<uint8_t>(*count - 1); i > 0; --i) items[i] = items[i - 1];

  snprintf(items[0].path, sizeof(items[0].path), "%s", path);
  snprintf(items[0].title, sizeof(items[0].title), "%s", title != nullptr ? title : "");
}

}  // namespace paperdiy
