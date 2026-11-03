#pragma once

// 表紙の無い本のために、題名を縦書きで描いた表紙を作る。
//
// 青空文庫のテキストにも、自分で入れた .txt にも表紙の絵は入っていない。
// 「絵が無いので何も出さない」だと本棚が白いままになるので、題名と著者を
// 組んだ紙面を、その本の表紙として描く。
//
// 追加のライブラリは要らない。本文で使っているフォントをそのまま借りる。

#include <stddef.h>
#include <stdint.h>

#include <render/TtfFont.h>

namespace paperdiy {

// title と author を width×height の紙に縦書きで組む。
//
// rows は 1bpp。並びは BMP に合わせて「1 = 白」で、y = 0 が上の行。
// 呼ぶ前に白（0xFF）で埋めておく必要はない（この関数が埋める）。
//
// maxSize は題名に使う文字の上限（px）。本棚の小さな表紙（75x126）と、
// 本を開いたときの扉（480x800）では、ちょうどよい大きさがまるで違うので、
// 呼ぶ側が決める。枠の太さと余白は紙の大きさから決める。
bool drawTitleCover(const char* title, const char* author, uint16_t width, uint16_t height,
                    uint16_t rowBytes, uint8_t* rows, freeink::book::FontChain& fonts,
                    uint16_t maxSize = 28);

}  // namespace paperdiy
