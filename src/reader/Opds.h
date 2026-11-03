#pragma once

// OPDS のカタログを読む。
//
// OPDS は Atom（XML）でできた「本の目録」で、青空文庫や Calibre、
// Project Gutenberg などが配っている。行き先をたどると目録が枝分かれし、
// 葉のところに本のダウンロード先がある。
//
// XML は FreeInkBook が抱えている expat を使う（SDK が「自前の XML 解析には
// これを使え、expat を二重に持つな」と言っている）。受け取ったバイトを
// そのまま流し込むので、応答を丸ごとメモリに置かない。

#include <stddef.h>
#include <stdint.h>

namespace paperdiy {

inline constexpr uint8_t kMaxOpdsEntries = 32;

enum class OpdsKind : uint8_t {
  Branch = 0,   // 次の目録へ
  Book,         // 落とせて、読める形式
  Unsupported,  // 落とせるが、この端末では読めない形式（PDF や ZIP）
};

struct OpdsEntry {
  char title[96] = {0};
  char author[64] = {0};
  char href[224] = {0};
  OpdsKind kind = OpdsKind::Branch;
};

// その入手リンクの「ありがたさ」。大きいほうを採る。
//   0 = 入手リンクではない（次の目録などへの行き先）
//   1 = 落とせるが読めない（PDF・xhtml など）
//   2 = ZIP。青空文庫のテキスト形式なら、落としてから EPUB に組み直せる
//   3 = テキスト
//   4 = EPUB
// 型が当てにならないサーバもあるので、URL の拡張子も見る。
uint8_t acquisitionRank(const char* rel, const char* type, const char* href);

// 読めるものとして扱う下限。これ未満は一覧に「非対応」と出す。
inline constexpr uint8_t kReadableRank = 2;

class OpdsFeed {
 public:
  void reset();
  // 受け取ったバイトを流し込む。最後は more = false で締める。
  bool feed(const char* data, size_t len, bool more);

  uint8_t count() const { return count_; }
  const OpdsEntry& at(uint8_t index) const { return items_[index]; }
  bool truncated() const { return truncated_; }

  const char* feedTitle() const { return title_; }
  const char* nextUrl() const { return next_; }
  const char* prevUrl() const { return prev_; }

 private:
  // expat から呼ばれる。
  static void onStart(void* user, const char* name, const char** attrs);
  static void onEnd(void* user, const char* name);
  static void onText(void* user, const char* text, int len);

  OpdsEntry items_[kMaxOpdsEntries];
  uint8_t count_ = 0;
  bool truncated_ = false;

  char title_[96] = {0};
  char next_[224] = {0};
  char prev_[224] = {0};
  uint8_t pendingRank_ = 0;  // いま覚えている入手リンクのありがたさ

  // 解析の途中の状態。
  void* parser_ = nullptr;
  OpdsEntry pending_;
  bool inEntry_ = false;
  bool inTitle_ = false;
  bool inAuthorName_ = false;
  bool inAuthor_ = false;
  bool bookLinkFound_ = false;
  int depth_ = 0;
  char text_[256] = {0};
  uint16_t textLen_ = 0;
};

// 相対 URL を、いま見ている場所を基準に絶対 URL へ直す。
void resolveUrl(const char* base, const char* href, char* out, size_t cap);
// URL の末尾から、保存するファイル名を作る。拡張子が無ければ ext を付ける。
void filenameFromUrl(const char* url, const char* fallback, char* out, size_t cap);

}  // namespace paperdiy
