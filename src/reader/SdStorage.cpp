#include "SdStorage.h"

#include <string.h>

#include <SDCardManager.h>

namespace paperdiy {

namespace {
SDCardManager& sd() { return SDCardManager::getInstance(); }
}  // namespace

// --- SdBookSource -----------------------------------------------------------

bool SdBookSource::open(const char* path) {
  close();
  file_ = sd().open(path, O_RDONLY);
  if (!file_) return false;
  size_ = file_.size();
  pos_ = 0;
  open_ = true;
  return true;
}

void SdBookSource::close() {
  if (open_) {
    file_.close();
    open_ = false;
  }
  size_ = 0;
  pos_ = 0;
}

int32_t SdBookSource::readAt(uint64_t offset, void* dst, uint32_t len) {
  if (!open_) return -1;
  if (offset != pos_) {
    if (!file_.seekSet(offset)) return -1;
    pos_ = offset;
  }
  const int got = file_.read(dst, len);
  if (got < 0) return -1;
  pos_ += static_cast<uint32_t>(got);
  return got;
}

// --- SdCacheStorage ---------------------------------------------------------

void SdCacheStorage::fullPath(const char* name, char* out, size_t cap) const {
  snprintf(out, cap, "%s/%s", dir_, name);
}

bool SdCacheStorage::begin(const char* dir) {
  end();
  snprintf(dir_, sizeof(dir_), "%s", dir);
  return sd().mkdir(dir_, true) || sd().exists(dir_);
}

void SdCacheStorage::end() {
  if (writing_) {
    writeFile_.close();
    writing_ = false;
  }
  dir_[0] = '\0';
}

void SdCacheStorage::clearAll() {
  if (dir_[0] == '\0') return;
  FsFile dir = sd().open(dir_, O_RDONLY);
  if (!dir) return;
  FsFile entry;
  char name[96];
  while (entry.openNext(&dir, O_RDONLY)) {
    if (!entry.isDir() && entry.getName(name, sizeof(name))) {
      char path[192];
      fullPath(name, path, sizeof(path));
      entry.close();
      sd().remove(path);
      continue;
    }
    entry.close();
  }
  dir.close();
}

bool SdCacheStorage::exists(const char* name) {
  char path[192];
  fullPath(name, path, sizeof(path));
  return sd().exists(path);
}

bool SdCacheStorage::remove(const char* name) {
  char path[192];
  fullPath(name, path, sizeof(path));
  return sd().remove(path);
}

int64_t SdCacheStorage::fileSize(const char* name) {
  char path[192];
  fullPath(name, path, sizeof(path));
  FsFile f = sd().open(path, O_RDONLY);
  if (!f) return -1;
  const int64_t size = static_cast<int64_t>(f.size());
  f.close();
  return size;
}

int32_t SdCacheStorage::readAt(const char* name, uint32_t offset, void* dst, uint32_t len) {
  char path[192];
  fullPath(name, path, sizeof(path));
  FsFile f = sd().open(path, O_RDONLY);
  if (!f) return -1;
  if (!f.seekSet(offset)) {
    f.close();
    return -1;
  }
  const int got = f.read(dst, len);
  f.close();
  return got < 0 ? -1 : got;
}

bool SdCacheStorage::beginWrite(const char* name) {
  if (writing_) endWrite();
  snprintf(writeName_, sizeof(writeName_), "%s", name);
  char path[192];
  snprintf(path, sizeof(path), "%s/%s.tmp", dir_, name);
  sd().remove(path);
  // 読み書き両用で開く：書きながら既に書いたページを読み返せる。
  writeFile_ = sd().open(path, O_RDWR | O_CREAT | O_TRUNC);
  if (!writeFile_) return false;
  writePos_ = 0;
  writing_ = true;
  return true;
}

bool SdCacheStorage::write(const void* data, uint32_t len) {
  if (!writing_) return false;
  if (writeFile_.curPosition() != writePos_) {
    if (!writeFile_.seekSet(writePos_)) return false;
  }
  const size_t put = writeFile_.write(data, len);
  if (put != len) return false;
  writePos_ += len;
  return true;
}

bool SdCacheStorage::endWrite() {
  if (!writing_) return false;
  writeFile_.sync();
  writeFile_.close();
  writing_ = false;

  char tmp[192];
  char dst[192];
  snprintf(tmp, sizeof(tmp), "%s/%s.tmp", dir_, writeName_);
  fullPath(writeName_, dst, sizeof(dst));
  sd().remove(dst);
  // 途中で電源が落ちても、古いファイルか無かのどちらかしか残らない。
  return sd().rename(tmp, dst);
}

int32_t SdCacheStorage::readBackAt(uint32_t offset, void* dst, uint32_t len) {
  if (!writing_) return -1;
  const uint32_t saved = writePos_;
  if (!writeFile_.seekSet(offset)) return -1;
  const int got = writeFile_.read(dst, len);
  writeFile_.seekSet(saved);
  return got < 0 ? -1 : got;
}

}  // namespace paperdiy
