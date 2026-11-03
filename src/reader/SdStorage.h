#pragma once

// SD カード上の EPUB とページキャッシュを FreeInkBook につなぐアダプタ。
// エンジンはファイルシステムを一切知らないので、必要なのはこの 2 つだけ。

#include <SdFat.h>

#include <BookStorage.h>

namespace paperdiy {

// 開いている本 1 冊へのランダムアクセス。
class SdBookSource : public freeink::book::BookSource {
 public:
  bool open(const char* path);
  void close();
  bool isOpen() const { return open_; }

  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override;
  uint64_t size() const override { return size_; }

 private:
  FsFile file_;
  uint64_t size_ = 0;
  uint64_t pos_ = 0;
  bool open_ = false;
};

// 1 冊ぶんのページキャッシュ置き場（本ごとのディレクトリ）。
// 書き込み中のファイルも読み返せる（readBackAt）ので、章を組みながら
// 先頭ページを表示できる。
class SdCacheStorage : public freeink::book::CacheStorage {
 public:
  // dir は "/.jareader/cache/ab12cd34" のような本ごとのディレクトリ。
  bool begin(const char* dir);
  void end();
  // この本のキャッシュを全部消す（設定変更で世代が変わったとき用）。
  void clearAll();

  bool exists(const char* name) override;
  bool remove(const char* name) override;
  int64_t fileSize(const char* name) override;
  int32_t readAt(const char* name, uint32_t offset, void* dst, uint32_t len) override;
  bool beginWrite(const char* name) override;
  bool write(const void* data, uint32_t len) override;
  bool endWrite() override;
  int32_t readBackAt(uint32_t offset, void* dst, uint32_t len) override;

 private:
  void fullPath(const char* name, char* out, size_t cap) const;

  char dir_[96] = {0};
  char writeName_[96] = {0};
  FsFile writeFile_;
  uint32_t writePos_ = 0;
  bool writing_ = false;
};

}  // namespace paperdiy
