#include "Buttons.h"

namespace pdos {

void ButtonDecoder::seed(bool a, bool b, uint32_t nowMs) {
  rawA_ = stableA_ = a;
  rawB_ = stableB_ = b;
  changedA_ = changedB_ = nowMs;
  downSince_ = nowMs;
  consumed_ = false;
  firstIsA_ = a;
}

Key ButtonDecoder::update(bool a, bool b, uint32_t nowMs) {
  if (a != rawA_) {
    rawA_ = a;
    changedA_ = nowMs;
  }
  if (b != rawB_) {
    rawB_ = b;
    changedB_ = nowMs;
  }

  const bool wasDown = stableA_ || stableB_;
  if (stableA_ != rawA_ && nowMs - changedA_ >= kDebounceMs) stableA_ = rawA_;
  if (stableB_ != rawB_ && nowMs - changedB_ >= kDebounceMs) stableB_ = rawB_;
  const bool isDown = stableA_ || stableB_;

  if (!wasDown && isDown) {
    downSince_ = nowMs;
    consumed_ = false;
    firstIsA_ = stableA_;
  }

  if (consumed_) {
    if (!isDown) consumed_ = false;
    return Key::None;
  }

  if (stableA_ && stableB_) {
    consumed_ = true;
    return Key::Chord;
  }
  if (isDown && nowMs - downSince_ >= kLongMs) {
    consumed_ = true;
    return stableA_ ? Key::LongA : Key::LongB;
  }
  if (wasDown && !isDown) return firstIsA_ ? Key::A : Key::B;
  return Key::None;
}

}  // namespace pdos
