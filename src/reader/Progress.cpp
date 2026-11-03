#include "Progress.h"

namespace paperdiy {
namespace {

// 表が無いときの代わり。章 1 つを 1000 と数える（＝章数での均等割）。
constexpr uint32_t kFlatWeight = 1000;

}  // namespace

uint32_t cumulativeAt(const uint32_t* cumulative, uint16_t count, uint16_t index) {
  if (count == 0) return 0;
  if (index >= count) index = static_cast<uint16_t>(count - 1);
  if (cumulative != nullptr) return cumulative[index];
  return (static_cast<uint32_t>(index) + 1u) * kFlatWeight;
}

uint16_t progressPermille(const uint32_t* cumulative, uint16_t count, uint16_t spine, uint32_t page,
                          uint32_t pages) {
  if (count == 0) return 0;
  const uint32_t total = cumulativeAt(cumulative, count, static_cast<uint16_t>(count - 1));
  if (total == 0) return 0;

  const uint32_t prev = spine > 0 ? cumulativeAt(cumulative, count, static_cast<uint16_t>(spine - 1))
                                  : 0;
  const uint32_t cum = cumulativeAt(cumulative, count, spine);
  const uint32_t span = cum > prev ? cum - prev : 0;

  // 章の中は「読み終えたページ数 / 総ページ数」で測る。最後のページにいるとき
  // ちょうど章を読み切った扱いになるので、本の末尾で 1000 に届く。
  const uint32_t within = pages > 0 ? static_cast<uint32_t>(
                                          (static_cast<uint64_t>(page + 1u) * span) / pages)
                                    : span;

  const uint64_t value = (static_cast<uint64_t>(prev + within) * 1000u) / total;
  return static_cast<uint16_t>(value > 1000 ? 1000 : value);
}

bool jumpTargetFor(const uint32_t* cumulative, uint16_t count, uint16_t permille, JumpTarget* out) {
  if (count == 0 || out == nullptr) return false;
  if (permille > 1000) permille = 1000;

  const uint32_t total = cumulativeAt(cumulative, count, static_cast<uint16_t>(count - 1));
  if (total == 0) return false;

  uint64_t target = (static_cast<uint64_t>(total) * permille) / 1000u;
  if (target >= total) target = total - 1;  // 100% は最後の章の中に落とす

  // target を含む最初の章。累積は単調増加なので前から見ていけばよい。
  uint16_t spine = static_cast<uint16_t>(count - 1);
  uint32_t prev = 0;
  for (uint16_t s = 0; s < count; ++s) {
    const uint32_t cum = cumulativeAt(cumulative, count, s);
    if (target < cum) {
      spine = s;
      break;
    }
    prev = cum;
  }

  out->spine = spine;
  out->within = static_cast<uint32_t>(target - prev);
  const uint32_t cum = cumulativeAt(cumulative, count, spine);
  out->span = cum > prev ? cum - prev : 0;
  return true;
}

}  // namespace paperdiy
