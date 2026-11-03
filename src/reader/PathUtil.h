#pragma once

// SD カード（FAT32）へ書くファイル名を作るための小物。
// スクリーンショットの書名と、ネットから落とした本のファイル名で使う。

#include <stddef.h>
#include <stdint.h>

namespace paperdiy {

// 本のパスから、その本の作業ファイル（進捗・しおり・ページキャッシュ・
// 表紙）を置く名前を作る。FNV-1a。
//
// 同じ値を Library・CoverThumb・Aozora が使う。別々に持つと、片方だけ
// 直したときに既存のキャッシュが迷子になるので、ここに 1 つだけ置く。
uint32_t pathHash(const char* path);

// FAT32 で使えない文字を '-' に置き換えて out へ詰める。
// 置き換えるのは \ / : * ? " < > | と空白と制御文字。
// cap-1 バイトに収まらないときは UTF-8 の文字の途中で切らずに縮める。
void sanitizeFatName(const char* in, char* out, size_t cap);

// UTF-8 の文字の途中で切らないように、末尾を削って長さを整える。
void trimUtf8(char* s);

}  // namespace paperdiy
