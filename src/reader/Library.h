#pragma once

// SD カードの本棚。/books を起点に、フォルダをたどって本を選ぶ。
//
// メモリに載せるのは「いま開いているフォルダ 1 つぶん」だけ。深い階層に
// 潜っても使う量は変わらない。いた場所は現在のパス 1 本で覚える。
//
// EPUB のタイトルは一度読んだら /.jareader/library.bin に控えるので、
// 2 回目以降は開き直さない。

#include <stddef.h>
#include <stdint.h>

#include <BookArena.h>

#include "Config.h"

namespace paperdiy {

struct Entry {
  char name[128];   // フォルダ名、またはファイル名
  char title[104];  // EPUB のタイトル。読めなければファイル名
  bool isDir = false;
  bool isText = false;
  // 青空文庫のテキスト形式。開くときに EPUB へ組み直す。
  bool isZip = false;
};

class Library {
 public:
  bool begin(freeink::book::Arena* work);

  // フォルダを開いて中身を読む。dir が nullptr なら /books。
  void openDir(const char* dir);
  // いまのフォルダを読み直す。
  void refresh();

  // index のフォルダへ入る。ファイルなら何もせず false。
  bool enter(uint16_t index);
  // 親フォルダへ戻る。/books より上へは行かない。
  // 戻ったあと、出てきたフォルダを選び直せるよう、その位置を返す。
  bool up(int16_t* selectedOut);
  bool atRoot() const;
  const char* currentDir() const { return dir_; }
  // 画面に出す見出し（/books なら「本棚」、その下ならフォルダ名）。
  const char* dirLabel() const;

  uint16_t count() const { return count_; }
  const Entry& at(uint16_t index) const { return items_[index]; }
  // 読みきれなかったぶんがあるか（フォルダに kMaxEntries 個を超えて入っていた）。
  bool truncated() const { return truncated_; }

  // index のフルパスを組み立てる。
  void pathAt(uint16_t index, char* out, size_t cap) const;
  // 名前からその位置を探す。無ければ -1。
  int16_t indexOfName(const char* name) const;

  bool showHidden() const { return showHidden_; }
  void setShowHidden(bool show) { showHidden_ = show; }

  // index のファイルまたはフォルダを消す。フォルダは中身ごと消す。
  // 本を消すときは、その本のキャッシュ・しおり・進捗も一緒に片づける。
  bool removeAt(uint16_t index);

  // index より後ろにある本を、並び順に最大 max 件拾う（読了時の「次の本」）。
  uint8_t nextBooks(uint16_t from, uint16_t* out, uint8_t max) const;

  // その本に付いている作業ファイル（ページキャッシュ・しおり・進捗）を消す。
  // 本そのものは消さない。青空文庫の EPUB を組み直したときのように、
  // 中身が変わって前の読書位置が当てにならなくなったときに使う。
  void dropBookData(const char* path) const;

 private:
  bool lookupCachedTitle(uint32_t hash, char* out, size_t cap) const;
  void cacheTitle(uint32_t hash, const char* title) const;
  bool readEpubTitle(const char* path, char* out, size_t cap);
  void sortEntries();

  Entry* items_ = nullptr;
  uint16_t count_ = 0;
  bool truncated_ = false;
  bool showHidden_ = false;
  char dir_[192] = {0};
  freeink::book::Arena* work_ = nullptr;
};

}  // namespace paperdiy
