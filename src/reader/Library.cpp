#include "os/I18n.h"
#include "Library.h"

#include <Arduino.h>
#include <string.h>

#include <FreeInkBook.h>
#include <SDCardManager.h>

#include "Config.h"
#include "NaturalOrder.h"
#include "PathUtil.h"
#include "SdStorage.h"

namespace paperdiy {
namespace {

namespace fb = freeink::book;

SDCardManager& sd() { return SDCardManager::getInstance(); }

struct TitleRecord {
  uint32_t hash;
  char title[104];
};

bool hasExt(const char* name, const char* ext) {
  const size_t ln = strlen(name), le = strlen(ext);
  return ln > le && strcasecmp(name + ln - le, ext) == 0;
}

// フォルダを消すための、たどっている途中の道。再帰にすると深い階層で
// スタックを使い切るので、自前の積み木にする。
constexpr uint8_t kWalkDepth = 16;

}  // namespace

bool Library::begin(fb::Arena* work) {
  work_ = work;
  const size_t bytes = sizeof(Entry) * kMaxEntries;
  void* buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (buf == nullptr) buf = malloc(bytes);
  items_ = static_cast<Entry*>(buf);
  snprintf(dir_, sizeof(dir_), "%s", kBooksDir);
  return items_ != nullptr;
}

bool Library::atRoot() const { return strcmp(dir_, kBooksDir) == 0; }

const char* Library::dirLabel() const {
  if (atRoot()) return TR("本棚", "Library");
  const char* slash = strrchr(dir_, '/');
  return slash != nullptr ? slash + 1 : dir_;
}

void Library::pathAt(uint16_t index, char* out, size_t cap) const {
  if (index >= count_) {
    if (cap > 0) out[0] = '\0';
    return;
  }
  snprintf(out, cap, "%s/%s", dir_, items_[index].name);
}

int16_t Library::indexOfName(const char* name) const {
  if (name == nullptr || items_ == nullptr) return -1;
  for (uint16_t i = 0; i < count_; ++i) {
    if (strcmp(items_[i].name, name) == 0) return static_cast<int16_t>(i);
  }
  return -1;
}

bool Library::lookupCachedTitle(uint32_t hash, char* out, size_t cap) const {
  FsFile f = sd().open(kLibraryPath, O_RDONLY);
  if (!f) return false;
  TitleRecord rec;
  bool found = false;
  while (f.read(&rec, sizeof(rec)) == static_cast<int>(sizeof(rec))) {
    if (rec.hash == hash) {
      rec.title[sizeof(rec.title) - 1] = '\0';
      snprintf(out, cap, "%s", rec.title);
      found = true;
      break;
    }
  }
  f.close();
  return found;
}

void Library::cacheTitle(uint32_t hash, const char* title) const {
  FsFile f = sd().open(kLibraryPath, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return;
  TitleRecord rec{};
  rec.hash = hash;
  snprintf(rec.title, sizeof(rec.title), "%s", title);
  f.write(&rec, sizeof(rec));
  f.close();
}

bool Library::readEpubTitle(const char* path, char* out, size_t cap) {
  if (work_ == nullptr) return false;
  SdBookSource src;
  if (!src.open(path)) return false;
  work_->reset();
  const size_t half = work_->capacity() / 2;
  fb::Arena a(work_->alloc(half, 8), half);
  fb::Arena b(work_->alloc(half - 64, 8), half - 64);
  fb::Book book;
  const bool ok = book.open(src, a, b) == fb::BookStatus::Ok;
  out[0] = '\0';
  if (ok && book.metadata().title != nullptr && book.metadata().title[0] != '\0') {
    snprintf(out, cap, "%s", book.metadata().title);
  }
  src.close();
  work_->reset();
  return ok && out[0] != '\0';
}

// フォルダを先に、そのあと自然順。一覧の見た目と「次の本」の順序が揃う。
void Library::sortEntries() {
  for (uint16_t i = 1; i < count_; ++i) {
    Entry key = items_[i];
    int32_t j = static_cast<int32_t>(i) - 1;
    while (j >= 0) {
      const Entry& left = items_[j];
      bool keyFirst;
      if (left.isDir != key.isDir) {
        keyFirst = key.isDir;  // フォルダを上に
      } else {
        keyFirst = naturalLess(key.title, left.title);
      }
      if (!keyFirst) break;
      items_[j + 1] = items_[j];
      --j;
    }
    items_[j + 1] = key;
  }
}

void Library::openDir(const char* dir) {
  if (dir != nullptr) snprintf(dir_, sizeof(dir_), "%s", dir);
  refresh();
}

void Library::refresh() {
  count_ = 0;
  truncated_ = false;
  if (items_ == nullptr) return;
  sd().mkdir(dir_, true);

  FsFile folder = sd().open(dir_, O_RDONLY);
  if (!folder) {
    Serial.printf("[library] %s を開けません\n", dir_);
    return;
  }

  FsFile entry;
  char name[160];
  while (entry.openNext(&folder, O_RDONLY)) {
    const bool isDir = entry.isDir();
    const bool named = entry.getName(name, sizeof(name));
    entry.close();
    if (!named) continue;
    if (!showHidden_ && name[0] == '.') continue;
    // アプリの作業フォルダは、隠しを出す設定でも本棚には並べない。
    if (strcmp(name, "System Volume Information") == 0) continue;

    const bool epub = hasExt(name, ".epub");
    const bool text = hasExt(name, ".txt");
    // 青空文庫のテキスト形式（ZIP）も並べる。開くときに EPUB へ組み直す。
    const bool zip = hasExt(name, ".zip");
    if (!isDir && !epub && !text && !zip) continue;

    if (count_ >= kMaxEntries) {
      truncated_ = true;
      break;
    }

    Entry& item = items_[count_];
    snprintf(item.name, sizeof(item.name), "%s", name);
    item.isDir = isDir;
    item.isText = text;
    item.isZip = zip;

    if (isDir) {
      snprintf(item.title, sizeof(item.title), "%s", name);
    } else {
      char path[192];
      snprintf(path, sizeof(path), "%s/%s", dir_, name);
      const uint32_t hash = pathHash(path);
      if (!lookupCachedTitle(hash, item.title, sizeof(item.title))) {
        if (!epub || !readEpubTitle(path, item.title, sizeof(item.title))) {
          snprintf(item.title, sizeof(item.title), "%s", name);
        }
        cacheTitle(hash, item.title);
      }
    }
    ++count_;
  }
  folder.close();

  sortEntries();
  Serial.printf("[library] %s に %u 件%s\n", dir_, count_, truncated_ ? "（打ち切り）" : "");
}

bool Library::enter(uint16_t index) {
  if (index >= count_ || !items_[index].isDir) return false;
  char next[192];
  snprintf(next, sizeof(next), "%s/%s", dir_, items_[index].name);
  openDir(next);
  return true;
}

bool Library::up(int16_t* selectedOut) {
  if (atRoot()) return false;
  // いま出てきたフォルダの名前を控えてから、親へ戻る。
  char leaving[128];
  const char* slash = strrchr(dir_, '/');
  snprintf(leaving, sizeof(leaving), "%s", slash != nullptr ? slash + 1 : "");

  char parent[192];
  snprintf(parent, sizeof(parent), "%s", dir_);
  char* cut = strrchr(parent, '/');
  if (cut == nullptr || cut == parent) return false;
  *cut = '\0';
  openDir(parent);

  if (selectedOut != nullptr) {
    const int16_t back = indexOfName(leaving);
    *selectedOut = back >= 0 ? back : 0;
  }
  return true;
}

void Library::dropBookData(const char* path) const {
  char id[16];
  snprintf(id, sizeof(id), "%08lx", static_cast<unsigned long>(pathHash(path)));

  char work[192];
  snprintf(work, sizeof(work), "%s/%s.pos", kStateDir, id);
  sd().remove(work);
  snprintf(work, sizeof(work), "%s/%s.bmk", kStateDir, id);
  sd().remove(work);
  snprintf(work, sizeof(work), "%s/%s", kCacheDir, id);
  sd().removeDir(work);
}

bool Library::removeAt(uint16_t index) {
  if (index >= count_) return false;

  char path[192];
  pathAt(index, path, sizeof(path));

  if (!items_[index].isDir) {
    dropBookData(path);
    const bool ok = sd().remove(path);
    if (ok) refresh();
    return ok;
  }

  // フォルダは、たどりながら中の本の作業ファイルも片づける。
  // 中身を消してからでないと親を消せないので、行きに積んで帰りに消す。
  struct Step {
    char path[192];
    bool done;  // true = 中身は片づけた。あとは自分を消すだけ
  };
  // 3KB ほどあるので、関数のスタックには置かない。
  static Step stack[kWalkDepth];
  snprintf(stack[0].path, sizeof(stack[0].path), "%s", path);
  stack[0].done = false;
  int16_t top = 1;

  bool ok = true;
  while (top > 0 && ok) {
    Step& step = stack[top - 1];
    if (step.done) {
      ok = sd().rmdir(step.path);
      --top;
      continue;
    }
    step.done = true;

    FsFile folder = sd().open(step.path, O_RDONLY);
    if (!folder) {
      ok = false;
      break;
    }
    FsFile child;
    char name[160];
    while (child.openNext(&folder, O_RDONLY)) {
      const bool isDir = child.isDir();
      const bool named = child.getName(name, sizeof(name));
      child.close();
      if (!named) continue;

      char childPath[192];
      snprintf(childPath, sizeof(childPath), "%s/%s", step.path, name);
      if (isDir) {
        if (top >= kWalkDepth) {  // これ以上は潜らない
          ok = false;
          break;
        }
        snprintf(stack[top].path, sizeof(stack[top].path), "%s", childPath);
        stack[top].done = false;
        ++top;
      } else {
        dropBookData(childPath);
        if (!sd().remove(childPath)) {
          ok = false;
          break;
        }
      }
    }
    folder.close();
  }

  refresh();
  return ok;
}

uint8_t Library::nextBooks(uint16_t from, uint16_t* out, uint8_t max) const {
  uint8_t found = 0;
  for (uint16_t i = from + 1; i < count_ && found < max; ++i) {
    if (items_[i].isDir) continue;
    out[found++] = i;
  }
  return found;
}

}  // namespace paperdiy
