#pragma once

// 最近読んだ本。新しい順に kMaxRecent 冊。
//
// 本を開いたときに先頭へ入れ直す。読み込むときに、もう無くなっている本は
// 落とす（SD から消したり、PC で移動したりしたもの）。

#include <stddef.h>
#include <stdint.h>

#include "Config.h"

namespace paperdiy {

struct RecentBook {
  char path[192] = {0};
  char title[104] = {0};
};

// 並びの操作だけを取り出したもの（SD カードには触らない）。
// 同じ本があれば先頭へ動かし、無ければ入れる。満杯なら末尾を押し出す。
void recentInsert(RecentBook* items, uint8_t* count, uint8_t capacity, const char* path,
                  const char* title);
int16_t recentFind(const RecentBook* items, uint8_t count, const char* path);
void recentRemoveAt(RecentBook* items, uint8_t* count, uint8_t index);

class RecentBooks {
 public:
  void load();
  void add(const char* path, const char* title);
  void remove(const char* path);

  uint8_t count() const { return count_; }
  const RecentBook& at(uint8_t index) const { return items_[index]; }

 private:
  bool save() const;

  RecentBook items_[kMaxRecent];
  uint8_t count_ = 0;
};

}  // namespace paperdiy
