#pragma once

// 日本語 TTF の供給元。
//
// 和文フォントは 3〜5 MB あるので、置き場所が読書体験を左右する。
//
//   1. フラッシュの "font" パーティション（推奨）
//      esp_partition_mmap でそのままアドレス空間に見えるので、PSRAM を
//      1 バイトも使わない。tools/flash_font.sh で焼く。
//   2. SD カードの /fonts/*.ttf
//      PSRAM に読み込む。差し替えが楽なかわり PSRAM を数 MB 使う。
//
// stb_truetype はフォント全体がアドレス可能である必要があるが、mmap も
// PSRAM もその条件を満たす。

#include <stdint.h>

#include <BookArena.h>
#include <render/TtfFont.h>

namespace paperdiy {

class FontStore {
 public:
  static constexpr uint8_t kMaxFaces = 3;

  // フラッシュ → SD の順に探し、見つかったものを FontChain に登録する。
  bool begin();

  freeink::book::FontChain& chain() { return chain_; }
  bool ready() const { return faceCount_ > 0; }
  uint8_t faceCount() const { return faceCount_; }

  // レイアウトキャッシュの世代に混ぜる指紋。フォントを差し替えたら
  // 値が変わり、キャッシュが自動で作り直される。
  uint32_t fingerprint() const { return fingerprint_; }

  // "flash" / "sd" / "none" — 起動ログと設定画面の表示用。
  const char* sourceName() const { return source_; }
  uint32_t totalBytes() const { return totalBytes_; }

  // UI の日本語フォールバック用に、本文用の実フェイスを返す。
  freeink::book::TtfFont* primary() { return faceCount_ > 0 ? &faces_[0] : nullptr; }

 private:
  bool addFace(const uint8_t* data, uint32_t len, uint8_t styleFlags);
  bool loadFromFlash();
  bool loadFromSd();

  freeink::book::TtfFont faces_[kMaxFaces];
  freeink::book::Arena glyphArenas_[kMaxFaces];
  freeink::book::FontChain chain_;
  uint8_t faceCount_ = 0;
  uint32_t fingerprint_ = 0;
  uint32_t totalBytes_ = 0;
  const char* source_ = "none";
};

}  // namespace paperdiy
