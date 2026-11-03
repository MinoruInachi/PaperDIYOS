#pragma once

// 本ごとのしおり。
//
// 位置は読書位置と同じ「章 + 文字位置」で覚える（ReaderEngine の BookPosition）。
// ページ番号ではなく文字位置なので、文字の大きさや組み方を変えても、
// しおりを挟んだのと同じ一文に戻れる。

#include <stddef.h>
#include <stdint.h>

#include "Config.h"

namespace paperdiy {

struct Bookmark {
  uint16_t spine = 0;
  uint32_t charOffset = 0;
  uint16_t permille = 0;   // 一覧に出す進捗
  char summary[80] = {0};  // ページ冒頭の書き出し
};

// 並びの操作だけを取り出したもの（SD カードには触らない）。
// 新しいしおりが先頭に来る。ホストのテストでそのまま回せる。
int16_t bookmarkFind(const Bookmark* items, uint16_t count, uint16_t spine, uint32_t charOffset);
// 先頭へ挿す。満杯なら一番古い末尾を押し出す。
void bookmarkInsert(Bookmark* items, uint16_t* count, uint16_t capacity, const Bookmark& entry);
void bookmarkRemoveAt(Bookmark* items, uint16_t* count, uint16_t index);

class Bookmarks {
 public:
  // 本を開いたときに読み込む。bookId は ReaderEngine が使うものと同じ。
  void open(const char* bookId);
  void close();

  uint16_t count() const { return count_; }
  const Bookmark& at(uint16_t index) const { return items_[index]; }

  // 同じ場所のしおりを探す。無ければ -1。
  int16_t find(uint16_t spine, uint32_t charOffset) const;

  // 現在位置のしおりを付け外しする。付けたら true、外したら false。
  bool toggle(const Bookmark& entry);
  void removeAt(uint16_t index);

 private:
  bool save() const;

  char bookId_[16] = {0};
  Bookmark items_[kMaxBookmarks];
  uint16_t count_ = 0;
};

}  // namespace paperdiy
