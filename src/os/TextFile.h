#pragma once

// SD の小さなテキストファイルを行ごとに読む。設定ファイルと名札と一覧で使う。

#include <SDCardManager.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>

namespace pdos {
namespace textfile {

inline void trim(char* s) {
  size_t len = strlen(s);
  while (len > 0 && isspace(static_cast<uint8_t>(s[len - 1]))) s[--len] = '\0';
  size_t head = 0;
  while (s[head] != '\0' && isspace(static_cast<uint8_t>(s[head]))) ++head;
  if (head > 0) memmove(s, s + head, len - head + 1);
}

class Reader {
 public:
  ~Reader() {
    if (file_) file_.close();
  }
  bool open(const char* path) {
    file_ = SDCardManager::getInstance().open(path, O_RDONLY);
    return static_cast<bool>(file_);
  }
  // 1 行読む。改行は含めない。ファイルの終わりで false。長すぎる行は切る。
  bool readLine(char* out, size_t cap) {
    size_t n = 0;
    bool any = false;
    int c;
    while ((c = file_.read()) >= 0) {
      any = true;
      if (c == '\n') break;
      if (c == '\r') continue;
      if (n + 1 < cap) out[n++] = static_cast<char>(c);
    }
    out[n] = '\0';
    return any;
  }

 private:
  FsFile file_;
};

// 無ければ text を書く。利用者が直したものは上書きしない。
inline void writeIfMissing(const char* path, const char* text) {
  auto& sd = SDCardManager::getInstance();
  if (sd.exists(path)) return;
  FsFile f = sd.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return;
  f.write(reinterpret_cast<const uint8_t*>(text), strlen(text));
  f.close();
}

}  // namespace textfile
}  // namespace pdos
