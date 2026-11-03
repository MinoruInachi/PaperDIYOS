#pragma once

// 青空文庫のテキスト形式（ZIP）を EPUB に組み直す。
//
// 中身は Shift_JIS の .txt が 1 つと、挿絵があれば PNG が何枚か。
// これを UTF-8 の XHTML にして、挿絵ごと EPUB へ詰め直す。
//
// なぜ EPUB にするか: .txt のままでも本文は読めるが（縦組みもルビも効く）、
// 挿絵を置く先がない。EPUB にすれば、既にある挿絵の道（ChapterLayout →
// ImageRenderer）にそのまま乗る。
//
// 注記の扱い:
//   ｜漢字《かんじ》 / 漢字《かんじ》   → <ruby>
//   ［＃…（fig01.png、横W×縦H）入る］  → <img>
//   ［＃「〜」は大見出し］など          → <h1>/<h2>
//   そのほかの注記                      → 落とす（本文には出さない）

#include <stddef.h>
#include <stdint.h>

namespace paperdiy {

// 組み直しの作り方の版。
//
// ここを上げると、前の版で作った EPUB は「古い」と見なされ、次に開いた
// ときに作り直される。組み方を変えたら必ず上げること。上げ忘れると、
// 手元にある本には新しい作りが反映されない（扉を足したときに実際に
// そうなった）。
inline constexpr uint8_t kAozoraFormatVersion = 2;

// epubPath が、いまの版で組み直したものか。
// 版の控えが無い（＝前の版で作った、または手で置いた）なら false。
bool aozoraEpubIsCurrent(const char* epubPath);

// 進み具合を画面に出すためのコールバック。
using AozoraProgressFn = void (*)(void* user, const char* label, uint8_t percent);

// zipPath の青空文庫 ZIP を読んで、epubPath に EPUB を書く。
// title が空でなければ、それを書名にする（空なら本文の 1 行目から取る）。
bool convertAozoraZip(const char* zipPath, const char* epubPath, const char* title,
                      AozoraProgressFn progress = nullptr, void* user = nullptr);

// 本文の 1 行を XHTML に直す。注記の解釈はここに集めてある。
// out には終端まで書く。書いた長さを返す。
// headingOut には見出しの深さ（0 = 見出しでない、1 = 大見出し …）を返す。
size_t aozoraLineToXhtml(const char* line, char* out, size_t cap, uint8_t* headingOut);

// 挿絵の注記からファイル名と寸法を取り出す。
//   ［＃石鏃二つの図（fig42154_01.png、横119×縦207）入る］
// 見つからなければ false。
bool parseAozoraImage(const char* line, char* nameOut, size_t nameCap, uint16_t* widthOut,
                      uint16_t* heightOut);

}  // namespace paperdiy
