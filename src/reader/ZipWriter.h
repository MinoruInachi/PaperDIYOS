#pragma once

// ZIP を書く（無圧縮）。EPUB を組み立てるために使う。
//
// 圧縮はしない。SDK が抱えている miniz は展開だけを有効にしてあり、
// 圧縮を有効にすると ROM のシンボルとぶつかる危険があるため触らない。
// EPUB は無圧縮でも仕様どおりに読める（mimetype はむしろ無圧縮が必須）。
// 本文は数百 KB、挿絵は PNG で既に圧縮済みなので、縮まなくても困らない。
//
// 出力先はファイルそのものではなく、書き出しの口を渡す形にしてある。
// ハードにも SD カードにも依存しないので、ホストのテストでそのまま回せる。

#include <stddef.h>
#include <stdint.h>

namespace paperdiy {

// 書き出しの口。書けたら true。
using ZipSinkFn = bool (*)(void* user, const void* data, size_t len);

// 中に入れられるファイルの数。EPUB は mimetype・container.xml・content.opf・
// toc.ncx の 4 つに、章と挿絵が加わる。青空文庫の長編は章が数十になるので、
// 章の上限（Aozora.cpp の kMaxChapters）と挿絵の上限を足しても余る数にする。
//
// **1 件 76 バイトあるので、ZipWriter をスタックに置かないこと。**
inline constexpr uint8_t kMaxZipEntries = 96;

class ZipWriter {
 public:
  void begin(ZipSinkFn sink, void* user);

  // まとめて 1 つ書く（中身が手元にあるとき）。
  bool addFile(const char* name, const void* data, size_t len);

  // 大きいものを分けて渡す。**大きさと CRC は先に分かっている必要がある**
  // （後から書き足す「データ記述子」は使わない。使うと、読み手によっては
  // 開けなくなる）。呼び出し側は中身を一度なめて crc を出しておくこと。
  bool beginEntry(const char* name, uint32_t size, uint32_t crc);
  bool addData(const void* data, size_t len);
  bool endEntry();

  // 目録を書いて締める。
  bool finish();

  bool failed() const { return failed_; }

 private:
  bool write(const void* data, size_t len);
  bool writeU16(uint16_t v);
  bool writeU32(uint32_t v);

  struct Entry {
    char name[64];
    uint32_t crc;
    uint32_t size;
    uint32_t offset;  // local header の位置
  };

  ZipSinkFn sink_ = nullptr;
  void* user_ = nullptr;
  Entry entries_[kMaxZipEntries];
  uint8_t count_ = 0;
  uint32_t offset_ = 0;   // ここまでに書いたバイト数
  uint32_t written_ = 0;  // いま書いている 1 件で流した量
  bool inEntry_ = false;
  bool failed_ = false;
};

// ZIP が使う CRC-32（IEEE）。WifiStore のものと同じ多項式。
uint32_t zipCrc32(uint32_t crc, const uint8_t* data, size_t len);

}  // namespace paperdiy
