#pragma once

// JaBook — 縦組みページの描画
//
// JaVertical が組んだ book::Page を 1bpp / Gray8 フレームバッファへ描く。
// 画素の書き込み規約・回転変換・ディザは FreeInkBook の PageRenderer と
// 同一なので、UI の描画結果と混ぜても濃度が揃う。
//
// 回転 run（styleFlags に VertRotated）は、グリフを時計回り 90 度回して
// 上から下へ積む。それ以外の run は横組みとまったく同じ描き方をする。

#include <stdint.h>

#include <render/PageRenderer.h>
#include <render/TtfFont.h>

#include "JaVertical.h"

namespace jabook {

struct VerticalRenderParams {
  // 全角枠の上端からベースラインまでの比率（%）。
  // VerticalLayoutParams::emAscentPct と必ず同じ値にする。
  uint16_t emAscentPct = 88;
};

class VerticalPageRenderer {
 public:
  // ページの文字を描き、どのフォントでも出せなかった字の数を返す。
  static uint32_t renderText(const fb::Page& page, fb::FontChain& fonts,
                             const fb::FrameTarget& target, const VerticalRenderParams& params,
                             uint32_t* firstMissingOut = nullptr);
};

}  // namespace jabook
