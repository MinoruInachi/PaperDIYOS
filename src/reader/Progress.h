#pragma once

// 本のどのあたりを読んでいるかの計算。
//
// 章の重みは「展開後のバイト数」で測る。章の数で均等に割ると、短い扉と
// 長い本文の章が同じ幅を占めてしまい、進捗の数字が本文の量と合わなくなる。
//
// ここは Arduino にも SDK にも依存しない。ホストのテストでそのまま回せる。

#include <stdint.h>

namespace paperdiy {

// spine 0..index の累積サイズ表。cumulative が nullptr のときは章数で均等に割る
// （サイズが引けなかった本でも進捗は出す）。
uint32_t cumulativeAt(const uint32_t* cumulative, uint16_t count, uint16_t index);

// 本全体を 1000 とした、いまの位置。
// page は 0 始まり、pages は章の総ページ数。
uint16_t progressPermille(const uint32_t* cumulative, uint16_t count, uint16_t spine, uint32_t page,
                          uint32_t pages);

// 千分率から行き先を求めた結果。within / span が章の中での位置になる。
struct JumpTarget {
  uint16_t spine = 0;
  uint32_t within = 0;  // 章の先頭から数えたサイズ
  uint32_t span = 0;    // その章のサイズ
};

// permille（0..1000）の行き先を求める。count が 0 なら false。
bool jumpTargetFor(const uint32_t* cumulative, uint16_t count, uint16_t permille, JumpTarget* out);

}  // namespace paperdiy
