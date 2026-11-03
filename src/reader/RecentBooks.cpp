#include "RecentBooks.h"

#include <Arduino.h>
#include <string.h>

#include <SDCardManager.h>

#include "Config.h"

namespace paperdiy {
namespace {

SDCardManager& sd() { return SDCardManager::getInstance(); }

constexpr uint32_t kMagic = 0x4A415231u;  // JAR1

}  // namespace

void RecentBooks::load() {
  count_ = 0;
  FsFile f = sd().open(kRecentPath, O_RDONLY);
  if (!f) return;

  uint32_t magic = 0;
  uint8_t stored = 0;
  if (f.read(&magic, sizeof(magic)) == sizeof(magic) && magic == kMagic &&
      f.read(&stored, sizeof(stored)) == sizeof(stored)) {
    if (stored > kMaxRecent) stored = kMaxRecent;
    for (uint8_t i = 0; i < stored; ++i) {
      RecentBook book;
      if (f.read(&book, sizeof(book)) != static_cast<int>(sizeof(book))) break;
      book.path[sizeof(book.path) - 1] = '\0';
      book.title[sizeof(book.title) - 1] = '\0';
      // もう無くなっている本は並べない。
      if (!sd().exists(book.path)) continue;
      items_[count_++] = book;
    }
  }
  f.close();
}

bool RecentBooks::save() const {
  FsFile f = sd().open(kRecentPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return false;
  f.write(&kMagic, sizeof(kMagic));
  f.write(&count_, sizeof(count_));
  if (count_ > 0) f.write(items_, sizeof(RecentBook) * count_);
  f.close();
  return true;
}

void RecentBooks::add(const char* path, const char* title) {
  recentInsert(items_, &count_, kMaxRecent, path, title);
  save();
}

void RecentBooks::remove(const char* path) {
  const int16_t at = recentFind(items_, count_, path);
  if (at < 0) return;
  recentRemoveAt(items_, &count_, static_cast<uint8_t>(at));
  save();
}

}  // namespace paperdiy
