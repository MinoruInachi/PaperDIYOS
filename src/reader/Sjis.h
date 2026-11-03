#pragma once

// Shift_JIS（CP932）を UTF-8 に直す。
//
// 青空文庫のテキストは Shift_JIS で配られているので、読む前にここを通す。
// 変換表は tools/gen_sjis_table.py が作ったものをフラッシュに置いてあり、
// PSRAM も DRAM も使わない。
//
// Arduino にも SDK にも依存しない。ホストのテストでそのまま回せる。

#include <stddef.h>
#include <stdint.h>

namespace paperdiy {

// 1 文字ぶん読んで Unicode を返す。
// `len` は残りのバイト数。読んだバイト数を `used` に返す。
// 読めないバイトは U+FFFD（下駄）にして 1 バイト進む（そこで止まらない）。
uint32_t sjisNext(const uint8_t* src, size_t len, size_t* used);

// Unicode 1 文字を UTF-8 で out に書く。書いたバイト数を返す（最大 4）。
// cap が足りなければ 0。
size_t utf8Encode(uint32_t code, char* out, size_t cap);

// Shift_JIS の並びを UTF-8 に直して out へ。書いたバイト数（終端を除く）を返す。
// out には必ず終端が付く。入りきらないぶんは捨てるが、文字の途中では切らない。
size_t sjisToUtf8(const uint8_t* src, size_t len, char* out, size_t cap);

}  // namespace paperdiy
