#include "Bookmarks.h"

#include <Arduino.h>
#include <string.h>

#include <SDCardManager.h>

#include "Config.h"

namespace paperdiy {
namespace {

SDCardManager& sd() { return SDCardManager::getInstance(); }

// JAB1。読み書きの形は進捗ファイル（.pos）と同じで、構造体をそのまま流す。
constexpr uint32_t kMagic = 0x4A414231u;

void pathFor(const char* bookId, char* out, size_t cap) {
  snprintf(out, cap, "%s/%s.bmk", kStateDir, bookId);
}

}  // namespace

void Bookmarks::open(const char* bookId) {
  close();
  snprintf(bookId_, sizeof(bookId_), "%s", bookId);

  char path[192];
  pathFor(bookId_, path, sizeof(path));
  FsFile f = sd().open(path, O_RDONLY);
  if (!f) return;

  uint32_t magic = 0;
  uint16_t stored = 0;
  if (f.read(&magic, sizeof(magic)) == sizeof(magic) && magic == kMagic &&
      f.read(&stored, sizeof(stored)) == sizeof(stored)) {
    if (stored > kMaxBookmarks) stored = kMaxBookmarks;
    for (uint16_t i = 0; i < stored; ++i) {
      if (f.read(&items_[i], sizeof(Bookmark)) != static_cast<int>(sizeof(Bookmark))) break;
      items_[i].summary[sizeof(items_[i].summary) - 1] = '\0';
      ++count_;
    }
  }
  f.close();
}

void Bookmarks::close() {
  bookId_[0] = '\0';
  count_ = 0;
}

bool Bookmarks::save() const {
  if (bookId_[0] == '\0') return false;
  char path[192];
  pathFor(bookId_, path, sizeof(path));

  // 1 件も無くなったらファイルごと消す（空ファイルを残さない）。
  if (count_ == 0) {
    sd().remove(path);
    return true;
  }

  FsFile f = sd().open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return false;
  f.write(&kMagic, sizeof(kMagic));
  f.write(&count_, sizeof(count_));
  f.write(items_, sizeof(Bookmark) * count_);
  f.close();
  return true;
}

int16_t Bookmarks::find(uint16_t spine, uint32_t charOffset) const {
  return bookmarkFind(items_, count_, spine, charOffset);
}

bool Bookmarks::toggle(const Bookmark& entry) {
  const int16_t existing = find(entry.spine, entry.charOffset);
  if (existing >= 0) {
    removeAt(static_cast<uint16_t>(existing));
    return false;
  }
  bookmarkInsert(items_, &count_, kMaxBookmarks, entry);
  save();
  return true;
}

void Bookmarks::removeAt(uint16_t index) {
  bookmarkRemoveAt(items_, &count_, index);
  save();
}

}  // namespace paperdiy
