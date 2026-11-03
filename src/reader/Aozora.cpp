#include "os/I18n.h"
#include "Aozora.h"

#include <Arduino.h>
#include <new>
#include <string.h>

#include <BookArena.h>
#include <SDCardManager.h>
#include <epub/ZipCatalog.h>

#include "Config.h"
#include "SdStorage.h"
#include "PathUtil.h"
#include "Sjis.h"
#include "ZipWriter.h"

namespace paperdiy {
namespace {

namespace fb = freeink::book;

SDCardManager& sd() { return SDCardManager::getInstance(); }

// 章 1 つぶんの XHTML をためる大きさ。これを超えたら段落の切れ目で章を分ける。
// 1 章が大きすぎると初回の組版に時間がかかり、小さすぎると章が増えすぎる。
constexpr size_t kChapterBytes = 48u * 1024;
// 挿絵 1 枚の上限。青空文庫の挿絵は数十 KB なので十分。
constexpr size_t kMaxImageBytes = 1024u * 1024;
constexpr size_t kLineBytes = 4096;
constexpr uint8_t kMaxChapters = 64;
constexpr uint8_t kMaxImages = 8;

// EPUB に入れるものは、章と挿絵に加えて mimetype・container.xml・
// content.opf・toc.ncx・扉（cover.xhtml）の 5 つ。ZIP 側の上限がこれを
// 下回っていると、長い本で途中まで書いたところで書けなくなる（実際に
// そうなった）。
static_assert(kMaxChapters + kMaxImages + 5 <= kMaxZipEntries,
              "ZipWriter の kMaxZipEntries が、章と挿絵の数に足りていません");

// 組み直しの途中で使う紙の束。
//
// **全部まとめて PSRAM に置く。**Arduino の loop タスクのスタックは 32KB
// （main.cpp の SET_LOOP_TASK_STACK_SIZE）しかなく、ここにある配列を素直に
// 関数の中に置くと、それだけで 50KB を超えて踏み越える。長い本ほど深い枝を
// 通るので、途中でリセットがかかる。
struct Work {
  uint8_t raw[kLineBytes];         // Shift_JIS のまま読んだ窓
  char line[kLineBytes];           // それを UTF-8 に直した 1 行
  char xhtml[kLineBytes * 2];      // 記法をほどいた 1 行
  char para[kLineBytes * 2 + 16];  // 段落の札に包んだもの
  char head[kLineBytes * 2 + 32];  // 見出しの札に包んだもの
  char pending[kLineBytes * 2];    // 巻き戻すかもしれない直前の行
  char meta[16384];                // content.opf
  char ncx[16384];                 // toc.ncx
  // 目録の控えを 96 件ぶん抱えていて 7KB ある。ここも積んではいけない。
  ZipWriter zip;
};

bool endsWithNoCase(const char* s, const char* suffix) {
  const size_t ls = strlen(s), lt = strlen(suffix);
  return ls >= lt && strcasecmp(s + ls - lt, suffix) == 0;
}

bool isImageName(const char* name) {
  return endsWithNoCase(name, ".png") || endsWithNoCase(name, ".jpg") ||
         endsWithNoCase(name, ".jpeg") || endsWithNoCase(name, ".gif");
}

// ZIP の中の名前からディレクトリを落とす（"files/fig01.png" → "fig01.png"）。
const char* baseName(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash != nullptr ? slash + 1 : path;
}

// 書き出し先。ZipWriter からここへ流れてくる。
struct FileSink {
  FsFile file;
  bool failed = false;
};

bool intoFile(void* user, const void* data, size_t len) {
  FileSink* sink = static_cast<FileSink*>(user);
  if (sink->file.write(data, len) != static_cast<int>(len)) {
    sink->failed = true;
    return false;
  }
  return true;
}

// 青空文庫のテキストは、本文の前後に決まった飾りが付いている。
//   タイトル / 著者名
//   ---- で囲まれた「記号について」の説明
//   （本文）
//   底本：…
// 説明と奥付は読むときに邪魔なので落とす。
struct TextState {
  uint8_t dashSeen = 0;   // ---- の行を何回見たか
  bool inHeader = true;   // まだ本文に入っていない
  bool done = false;      // 底本より後
  uint8_t metaLines = 0;  // 先頭のタイトル・著者を数える
};

// 日本語（というより ASCII 以外）を含むか。
// 青空文庫の見出しは「題名／英語の原題／著者」の順に並ぶことがあり、
// 原題を著者と取り違えないために使う。
bool hasNonAscii(const char* s) {
  for (const char* p = s; *p != '\0'; ++p) {
    if (static_cast<uint8_t>(*p) >= 0x80) return true;
  }
  return false;
}

bool isDashLine(const char* line) {
  if (line[0] != '-') return false;
  for (const char* p = line; *p != '\0'; ++p) {
    if (*p != '-') return false;
  }
  return true;
}

// 末尾に足す。入りきらなければ何も書かずに false。
//
// strncat だと溢れたぶんが黙って落ちて、壊れた content.opf が出来上がる。
// 途中で切れた目録は EPUB を開けなくするので、気づけるようにしておく。
bool appendStr(char* out, size_t cap, const char* s) {
  const size_t len = strlen(out), add = strlen(s);
  if (len + add + 1 > cap) return false;
  memcpy(out + len, s, add + 1);
  return true;
}

// XML の文字参照へ逃がしながら書く（メタデータ用）。
void appendEscaped(char* out, size_t cap, const char* s) {
  size_t len = strlen(out);
  for (const char* p = s; *p != '\0' && len + 8 < cap; ++p) {
    switch (*p) {
      case '&': memcpy(out + len, "&amp;", 5); len += 5; break;
      case '<': memcpy(out + len, "&lt;", 4); len += 4; break;
      case '>': memcpy(out + len, "&gt;", 4); len += 4; break;
      default: out[len++] = *p; break;
    }
  }
  out[len] = '\0';
}

// 版の控えの置き場所。本ごとのキャッシュに紛れ込ませておく。
// 本を消すと（Library::dropBookData）このフォルダごと消えるので、
// 控えだけが残ることはない。
void versionPath(const char* epubPath, char* out, size_t cap) {
  snprintf(out, cap, "%s/%08lx/aozora.ver", kCacheDir,
           static_cast<unsigned long>(pathHash(epubPath)));
}

}  // namespace

bool aozoraEpubIsCurrent(const char* epubPath) {
  if (epubPath == nullptr) return false;
  char path[192];
  versionPath(epubPath, path, sizeof(path));
  FsFile f = sd().open(path, O_RDONLY);
  if (!f) return false;
  uint8_t stored = 0;
  const bool read = f.read(&stored, 1) == 1;
  f.close();
  return read && stored == kAozoraFormatVersion;
}

bool convertAozoraZip(const char* zipPath, const char* epubPath, const char* title,
                      AozoraProgressFn progress, void* user) {
  if (zipPath == nullptr || epubPath == nullptr) return false;

  SdBookSource source;
  if (!source.open(zipPath)) {
    Serial.printf("[aozora] 開けません: %s\n", zipPath);
    return false;
  }

  // 作業場所は PSRAM から借りる。読み終えたら返す。
  const size_t arenaBytes = 256u * 1024;
  void* arenaBuf = heap_caps_malloc(arenaBytes, MALLOC_CAP_SPIRAM);
  char* chapter = static_cast<char*>(heap_caps_malloc(kChapterBytes, MALLOC_CAP_SPIRAM));
  Work* work = static_cast<Work*>(heap_caps_malloc(sizeof(Work), MALLOC_CAP_SPIRAM));
  if (arenaBuf == nullptr || chapter == nullptr || work == nullptr) {
    free(arenaBuf);
    free(chapter);
    free(work);
    source.close();
    Serial.println("[aozora] 作業領域を取れません");
    return false;
  }
  fb::Arena arena(arenaBuf, arenaBytes);

  // heap_caps_malloc は作りっぱなしなので、中の物は自分で組み立てる。
  new (&work->zip) ZipWriter();
  ZipWriter& zip = work->zip;

  bool ok = false;
  FileSink sink;

  do {
    fb::ZipCatalog catalog;
    if (catalog.open(source, arena) != fb::BookStatus::Ok) {
      Serial.println("[aozora] ZIP を読めません");
      break;
    }

    // 中身を見分ける。テキストは 1 つ、挿絵は何枚か。
    const fb::ZipEntry* textEntry = nullptr;
    const fb::ZipEntry* images[kMaxImages];
    uint8_t imageCount = 0;
    for (size_t i = 0; i < catalog.entryCount(); ++i) {
      const fb::ZipEntry* entry = catalog.entry(i);
      if (entry == nullptr || entry->name == nullptr) continue;
      if (textEntry == nullptr && endsWithNoCase(entry->name, ".txt")) {
        textEntry = entry;
      } else if (isImageName(entry->name) && imageCount < kMaxImages &&
                 entry->uncompressedSize <= kMaxImageBytes) {
        images[imageCount++] = entry;
      }
    }
    if (textEntry == nullptr) {
      Serial.println("[aozora] テキストが入っていません");
      break;
    }

    sink.file = sd().open(epubPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (!sink.file) {
      Serial.printf("[aozora] 書き込めません: %s\n", epubPath);
      break;
    }
    zip.begin(intoFile, &sink);

    // EPUB の決まりごと。mimetype は必ず先頭に、無圧縮で置く。
    const char* mimetype = "application/epub+zip";
    if (!zip.addFile("mimetype", mimetype, strlen(mimetype))) break;

    const char* container =
        "<?xml version=\"1.0\"?>\n"
        "<container version=\"1.0\" "
        "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
        "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
        "media-type=\"application/oebps-package+xml\"/></rootfiles>\n"
        "</container>\n";
    if (!zip.addFile("META-INF/container.xml", container, strlen(container))) break;

    // --- 本文を章ごとに XHTML へ ---
    const size_t mark = arena.mark();
    fb::ZipEntryReader reader;
    if (reader.open(source, *textEntry, arena) != fb::BookStatus::Ok) {
      Serial.println("[aozora] テキストを展開できません");
      break;
    }

    char bookTitle[128] = {0};
    char bookAuthor[128] = {0};
    if (title != nullptr) snprintf(bookTitle, sizeof(bookTitle), "%s", title);

    TextState state;
    uint8_t chapterCount = 0;
    size_t chapterLen = 0;
    bool chapterOpen = false;
    uint32_t consumed = 0;
    uint8_t lastPercent = 255;

    // Shift_JIS を 1 行ずつ拾うための小さな窓。中身は work の中（PSRAM）。
    uint8_t* const raw = work->raw;
    size_t rawLen = 0;
    char* const line = work->line;
    char* const xhtml = work->xhtml;
    // 直前に段落として書いた行。次の行が見出しの注記だったら巻き戻して
    // 見出しに置き直すので、中身と書き始めた位置を控えておく。
    size_t pendingStart = 0;
    bool hasPending = false;
    size_t chapterHeadLen = 0;  // 章の骨組みだけの長さ（中身が空かの判定用）
    bool truncated = false;     // 章の数が足りず、途中で打ち切ったか

    auto openChapter = [&]() {
      chapterLen = 0;
      const int wrote = snprintf(chapter, kChapterBytes,
                                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                 "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
                                 "<head><meta charset=\"utf-8\"/><title></title></head>\n"
                                 "<body>\n");
      chapterLen = wrote > 0 ? static_cast<size_t>(wrote) : 0;
      chapterHeadLen = chapterLen;
      hasPending = false;
      chapterOpen = true;
    };

    auto closeChapter = [&]() -> bool {
      if (!chapterOpen) return true;
      const int wrote =
          snprintf(chapter + chapterLen, kChapterBytes - chapterLen, "</body>\n</html>\n");
      if (wrote > 0) chapterLen += static_cast<size_t>(wrote);
      char name[48];
      snprintf(name, sizeof(name), "OEBPS/text_%03u.xhtml", static_cast<unsigned>(chapterCount));
      if (!zip.addFile(name, chapter, chapterLen)) return false;
      ++chapterCount;
      chapterOpen = false;
      return true;
    };

    openChapter();

    bool textOk = true;
    while (textOk) {
      // 窓を埋める。
      if (rawLen < sizeof(work->raw)) {
        const int32_t got =
            reader.read(raw + rawLen, static_cast<uint32_t>(sizeof(work->raw) - rawLen));
        if (got > 0) {
          rawLen += static_cast<size_t>(got);
          consumed += static_cast<uint32_t>(got);
        } else if (rawLen == 0) {
          break;  // 読み切った
        }
      }

      // 改行までを 1 行とする。見つからなければ窓いっぱいを 1 行と見なす。
      size_t lineEnd = 0;
      while (lineEnd < rawLen && raw[lineEnd] != '\n') ++lineEnd;
      const bool hasNewline = lineEnd < rawLen;
      size_t take = lineEnd;
      // CR を落とす。
      if (take > 0 && raw[take - 1] == '\r') --take;

      sjisToUtf8(raw, take, line, sizeof(work->line));

      // 窓から消費したぶんを詰める。
      const size_t drop = hasNewline ? lineEnd + 1 : rawLen;
      memmove(raw, raw + drop, rawLen - drop);
      rawLen -= drop;

      // 前後の飾りを落とす。
      if (isDashLine(line)) {
        ++state.dashSeen;
        if (state.dashSeen >= 2) state.inHeader = false;
        continue;
      }
      if (strncmp(line, "底本：", 9) == 0) state.done = true;
      if (state.done) continue;
      if (state.inHeader) {
        // 最初の数行はタイトルと著者。1 本目の ---- より前だけを見る。
        if (line[0] != '\0' && state.dashSeen == 0) {
          if (state.metaLines == 0 && bookTitle[0] == '\0') {
            snprintf(bookTitle, sizeof(bookTitle), "%s", line);
          } else if (state.metaLines >= 1 && bookAuthor[0] == '\0' && hasNonAscii(line)) {
            // 題名の次が英語の原題という本がある（「赤毛連盟」なら
            // "THE RED-HEADED LEAGUE"）。英字だけの行は著者ではない。
            snprintf(bookAuthor, sizeof(bookAuthor), "%s", line);
          }
          ++state.metaLines;
          // ---- で囲まれた説明が無い本もある。題と著者ぶんを過ぎたら、
          // そこからは本文として扱う（この行は落とさずに下へ流す）。
          if (state.metaLines > 3) state.inHeader = false;
        }
        if (state.inHeader) continue;
      }

      // ここから本文。
      char imageName[64];
      uint16_t imgW = 0, imgH = 0;
      if (parseAozoraImage(line, imageName, sizeof(imageName), &imgW, &imgH)) {
        char tag[192];
        if (imgW > 0 && imgH > 0) {
          snprintf(tag, sizeof(tag),
                   "<div><img src=\"%s\" width=\"%u\" height=\"%u\" alt=\"\"/></div>\n",
                   baseName(imageName), static_cast<unsigned>(imgW), static_cast<unsigned>(imgH));
        } else {
          snprintf(tag, sizeof(tag), "<div><img src=\"%s\" alt=\"\"/></div>\n",
                   baseName(imageName));
        }
        const size_t need = strlen(tag);
        if (chapterLen + need + 32 < kChapterBytes) {
          memcpy(chapter + chapterLen, tag, need);
          chapterLen += need;
        }
        continue;
      }

      uint8_t heading = 0;
      aozoraLineToXhtml(line, xhtml, sizeof(work->xhtml), &heading);

      // 青空文庫では、見出しは「本文の行」＋「次の行の注記」の 2 行で書かれる。
      //   第一章
      //   ［＃「第一章」は大見出し］
      // つまり注記を見た時点で、直前に段落として書いてしまった行が見出しだった
      // と分かる。その段落を巻き戻して、新しい章の頭に見出しとして置き直す。
      if (heading != 0 && xhtml[0] == '\0') {
        if (hasPending) {
          chapterLen = pendingStart;  // 段落として書いたぶんを取り消す
          hasPending = false;
          // 中身のある章だけ切る（章の頭に見出しが来たときに空の章を作らない）。
          if (chapterLen > chapterHeadLen && chapterCount + 1 < kMaxChapters) {
            if (!closeChapter()) {
              textOk = false;
              break;
            }
            openChapter();
          }
          char* const head = work->head;
          snprintf(head, sizeof(work->head), "<h%u>%s</h%u>\n", heading, work->pending, heading);
          const size_t headLen = strlen(head);
          if (chapterLen + headLen + 32 < kChapterBytes) {
            memcpy(chapter + chapterLen, head, headLen);
            chapterLen += headLen;
          }
        }
        continue;
      }

      char* const para = work->para;
      if (xhtml[0] == '\0') {
        snprintf(para, sizeof(work->para), "<p><br/></p>\n");  // 空行はそのまま間隔に
        hasPending = false;
      } else if (heading != 0) {
        snprintf(para, sizeof(work->para), "<h%u>%s</h%u>\n", heading, xhtml, heading);
        hasPending = false;
      } else {
        snprintf(para, sizeof(work->para), "<p>%s</p>\n", xhtml);
        // 次の行が見出しの注記だったときに巻き戻せるよう、控えておく。
        snprintf(work->pending, sizeof(work->pending), "%s", xhtml);
        hasPending = true;
      }
      const size_t need = strlen(para);

      // 章が大きくなりすぎたら、ここで切る。
      if (chapterLen + need + 64 >= kChapterBytes) {
        if (chapterCount + 1 >= kMaxChapters) {
          // これ以上章を増やせない。残りは切り捨てる。
          truncated = true;
          break;
        }
        if (!closeChapter()) {
          textOk = false;
          break;
        }
        openChapter();
      }
      pendingStart = chapterLen;
      if (chapterLen + need + 32 < kChapterBytes) {
        memcpy(chapter + chapterLen, para, need);
        chapterLen += need;
      }

      if (progress != nullptr && textEntry->uncompressedSize > 0) {
        const uint8_t percent = static_cast<uint8_t>(
            (static_cast<uint64_t>(consumed) * 90) / textEntry->uncompressedSize);
        if (percent != lastPercent) {
          lastPercent = percent;
          progress(user, TR("本文を組み直しています", "Converting the text"), percent);
        }
      }
    }

    if (truncated) {
      Serial.printf("[aozora] 章が %u で足りず、途中までにしました\n", kMaxChapters);
    }
    if (!textOk) {
      Serial.printf("[aozora] %u 章目を書けませんでした\n", chapterCount + 1);
      break;
    }
    if (!closeChapter()) break;
    arena.release(mark);

    // --- 挿絵をそのまま詰める ---
    uint8_t writtenImages = 0;
    for (uint8_t i = 0; i < imageCount; ++i) {
      const fb::ZipEntry* entry = images[i];
      uint8_t* buf = static_cast<uint8_t*>(
          heap_caps_malloc(entry->uncompressedSize, MALLOC_CAP_SPIRAM));
      if (buf == nullptr) continue;

      const size_t imgMark = arena.mark();
      fb::ZipEntryReader imgReader;
      bool got = imgReader.open(source, *entry, arena) == fb::BookStatus::Ok;
      uint32_t at = 0;
      while (got && at < entry->uncompressedSize) {
        const int32_t n = imgReader.read(buf + at, entry->uncompressedSize - at);
        if (n <= 0) break;
        at += static_cast<uint32_t>(n);
      }
      arena.release(imgMark);

      if (got && at == entry->uncompressedSize) {
        char name[80];
        snprintf(name, sizeof(name), "OEBPS/%s", baseName(entry->name));
        if (zip.addFile(name, buf, at)) ++writtenImages;
      }
      free(buf);
      if (progress != nullptr) {
        progress(user, TR("挿絵を入れています", "Adding illustrations"),
                 static_cast<uint8_t>(90 + (i + 1) * 8 / (imageCount > 0 ? imageCount : 1)));
      }
    }

    // --- 扉（題名と著者だけの 1 ページ）---
    //
    // 青空文庫のテキストには表紙が無い。開いた瞬間に本文が始まるのは
    // 素っ気ないので、題名と著者だけのページを先頭に置く。
    //
    // 書くのは最後になる。題名と著者はテキストの先頭を読み終えないと
    // 分からないため。読む順は content.opf の spine が決めるので、
    // ZIP の中で何番目に置いても、開けば最初に出る。
    //
    // 紙は work->head を借りる（本文を組み終えていて、もう使わない）。
    char* const cover = work->head;
    const size_t coverCap = sizeof(work->head);
    snprintf(cover, coverCap,
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
             "<head><meta charset=\"utf-8\"/><title></title></head>\n"
             "<body>\n<h1>");
    appendEscaped(cover, coverCap, bookTitle[0] != '\0' ? bookTitle : "無題");
    bool coverOk = appendStr(cover, coverCap, "</h1>\n");
    if (bookAuthor[0] != '\0') {
      coverOk &= appendStr(cover, coverCap, "<p>");
      appendEscaped(cover, coverCap, bookAuthor);
      coverOk &= appendStr(cover, coverCap, "</p>\n");
    }
    coverOk &= appendStr(cover, coverCap, "</body>\n</html>\n");
    if (!coverOk || !zip.addFile("OEBPS/cover.xhtml", cover, strlen(cover))) {
      Serial.println("[aozora] 扉を書けません");
      break;
    }

    // --- 目録（content.opf と toc.ncx）---
    //
    // 溢れたら黙って切れるのではなく、失敗として扱う。途中で切れた目録は
    // どのみち開けない EPUB になるので、半端なものを残すより作り直すほうがよい。
    char* const meta = work->meta;
    const size_t metaCap = sizeof(work->meta);
    bool metaOk = true;
    snprintf(meta, metaCap,
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" "
             "unique-identifier=\"id\">\n"
             "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
             "<dc:identifier id=\"id\">paperdiy-aozora</dc:identifier>\n"
             "<dc:language>ja</dc:language>\n"
             "<dc:title>");
    appendEscaped(meta, metaCap, bookTitle[0] != '\0' ? bookTitle : "無題");
    metaOk &= appendStr(meta, metaCap, "</dc:title>\n<dc:creator>");
    appendEscaped(meta, metaCap, bookAuthor);
    metaOk &= appendStr(meta, metaCap, "</dc:creator>\n");
    // 表紙。挿絵が入っていれば 1 枚目を表紙として指すので、本棚に絵が出る。
    if (writtenImages > 0) {
      metaOk &= appendStr(meta, metaCap, "<meta name=\"cover\" content=\"i0\"/>\n");
    }
    metaOk &= appendStr(meta, metaCap,
                        "</metadata>\n<manifest>\n"
                        "<item id=\"ncx\" href=\"toc.ncx\" "
                        "media-type=\"application/x-dtbncx+xml\"/>\n"
                        "<item id=\"cover\" href=\"cover.xhtml\" "
                        "media-type=\"application/xhtml+xml\"/>\n");

    for (uint8_t i = 0; i < chapterCount; ++i) {
      char item[128];
      snprintf(item, sizeof(item),
               "<item id=\"c%u\" href=\"text_%03u.xhtml\" media-type=\"application/xhtml+xml\"/>\n",
               i, i);
      metaOk &= appendStr(meta, metaCap, item);
    }
    for (uint8_t i = 0; i < imageCount && i < writtenImages; ++i) {
      const char* name = baseName(images[i]->name);
      const char* type = endsWithNoCase(name, ".png")   ? "image/png"
                         : endsWithNoCase(name, ".gif") ? "image/gif"
                                                        : "image/jpeg";
      char item[192];
      snprintf(item, sizeof(item),
               "<item id=\"i%u\" href=\"%s\" media-type=\"%s\"%s/>\n", i, name, type,
               i == 0 ? " properties=\"cover-image\"" : "");
      metaOk &= appendStr(meta, metaCap, item);
    }
    // 読む順。扉が先で、そのあと本文。
    metaOk &= appendStr(meta, metaCap,
                        "</manifest>\n<spine toc=\"ncx\">\n<itemref idref=\"cover\"/>\n");
    for (uint8_t i = 0; i < chapterCount; ++i) {
      char ref[48];
      snprintf(ref, sizeof(ref), "<itemref idref=\"c%u\"/>\n", i);
      metaOk &= appendStr(meta, metaCap, ref);
    }
    metaOk &= appendStr(meta, metaCap, "</spine>\n</package>\n");
    if (!metaOk) {
      Serial.println("[aozora] 目録が入りきりません");
      break;
    }
    if (!zip.addFile("OEBPS/content.opf", meta, strlen(meta))) break;

    char* const ncx = work->ncx;
    const size_t ncxCap = sizeof(work->ncx);
    bool ncxOk = true;
    snprintf(ncx, ncxCap,
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
             "<head><meta name=\"dtb:uid\" content=\"paperdiy-aozora\"/></head>\n"
             "<docTitle><text>");
    appendEscaped(ncx, ncxCap, bookTitle[0] != '\0' ? bookTitle : "無題");
    ncxOk &= appendStr(ncx, ncxCap, "</text></docTitle>\n<navMap>\n");
    ncxOk &= appendStr(ncx, ncxCap,
                       "<navPoint id=\"n0\" playOrder=\"1\"><navLabel><text>扉</text></navLabel>"
                       "<content src=\"cover.xhtml\"/></navPoint>\n");
    for (uint8_t i = 0; i < chapterCount; ++i) {
      char nav[192];
      snprintf(nav, sizeof(nav),
               "<navPoint id=\"n%u\" playOrder=\"%u\"><navLabel><text>%u</text></navLabel>"
               "<content src=\"text_%03u.xhtml\"/></navPoint>\n",
               i + 1, i + 2, i + 1, i);
      ncxOk &= appendStr(ncx, ncxCap, nav);
    }
    ncxOk &= appendStr(ncx, ncxCap, "</navMap>\n</ncx>\n");
    if (!ncxOk) {
      Serial.println("[aozora] 目次が入りきりません");
      break;
    }
    if (!zip.addFile("OEBPS/toc.ncx", ncx, strlen(ncx))) break;

    ok = zip.finish() && !sink.failed;
    if (ok) {
      // どの版で組んだかを控える。作りを変えたときに作り直せる。
      char verPath[192];
      versionPath(epubPath, verPath, sizeof(verPath));
      char verDir[192];
      snprintf(verDir, sizeof(verDir), "%s", verPath);
      char* slash = strrchr(verDir, '/');
      if (slash != nullptr) {
        *slash = '\0';
        sd().mkdir(verDir, true);
      }
      FsFile vf = sd().open(verPath, O_WRONLY | O_CREAT | O_TRUNC);
      if (vf) {
        const uint8_t v = kAozoraFormatVersion;
        vf.write(&v, 1);
        vf.close();
      }
      Serial.printf("[aozora] %s → %s（%u 章 / 挿絵 %u 枚）\n", zipPath, epubPath,
                    chapterCount, writtenImages);
    }
  } while (false);

  if (sink.file) sink.file.close();
  if (!ok) sd().remove(epubPath);  // 半端なものは残さない

  source.close();
  free(arenaBuf);
  free(chapter);
  free(work);
  return ok;
}

}  // namespace paperdiy
