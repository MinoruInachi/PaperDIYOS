#include "Bookmarks.h"

namespace paperdiy {

int16_t bookmarkFind(const Bookmark* items, uint16_t count, uint16_t spine, uint32_t charOffset) {
  if (items == nullptr) return -1;
  for (uint16_t i = 0; i < count; ++i) {
    if (items[i].spine == spine && items[i].charOffset == charOffset) {
      return static_cast<int16_t>(i);
    }
  }
  return -1;
}

void bookmarkInsert(Bookmark* items, uint16_t* count, uint16_t capacity, const Bookmark& entry) {
  if (items == nullptr || count == nullptr || capacity == 0) return;
  // 満杯のときは数を増やさない。末尾（一番古いもの）が押し出される。
  if (*count < capacity) ++(*count);
  for (uint16_t i = static_cast<uint16_t>(*count - 1); i > 0; --i) items[i] = items[i - 1];
  items[0] = entry;
  items[0].summary[sizeof(items[0].summary) - 1] = '\0';
}

void bookmarkRemoveAt(Bookmark* items, uint16_t* count, uint16_t index) {
  if (items == nullptr || count == nullptr || index >= *count) return;
  for (uint16_t i = index; i + 1 < *count; ++i) items[i] = items[i + 1];
  --(*count);
}

}  // namespace paperdiy
