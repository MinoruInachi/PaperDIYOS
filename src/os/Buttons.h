#pragma once

// 2 つのボタンから操作の種類を読み取る。
//
// PaperDIY の利用者ボタンは A（G4）と B（G3）の 2 つだけなので、Badger の
// 5 ボタンぶんの操作を押し方で出し分ける。
//
//   単押し   : 離した瞬間に A / B
//   長押し   : しきい値を超えた瞬間に LongA / LongB（離すのを待たない）
//   同時押し : 2 つそろった瞬間に Chord（Badger の A+C に当たる）
//
// ハードには触らない。時刻とボタンの状態を渡すだけなので、ホストで回せる。

#include <stdint.h>

namespace pdos {

enum class Key : uint8_t { None, A, B, LongA, LongB, Chord };

class ButtonDecoder {
 public:
  static constexpr uint32_t kDebounceMs = 15;
  static constexpr uint32_t kLongMs = 550;

  // 起動時にすでに押されているボタンを、押下の途中として受け付ける。
  void seed(bool a, bool b, uint32_t nowMs);

  // 周期的に呼ぶ。操作が確定したら返す。
  Key update(bool a, bool b, uint32_t nowMs);

  bool anyDown() const { return stableA_ || stableB_; }

 private:
  bool rawA_ = false, rawB_ = false;
  bool stableA_ = false, stableB_ = false;
  uint32_t changedA_ = 0, changedB_ = 0;
  uint32_t downSince_ = 0;
  bool consumed_ = false;  // この押下はもう操作として確定した
  bool firstIsA_ = false;
};

}  // namespace pdos
