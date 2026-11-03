#pragma once

// アプリ全体の定数。SD 上の置き場所とパーティション名。

#include <stdint.h>

namespace paperdiy {

// SD カードのディレクトリ構成
//   /books        …… 読みたい .epub / .txt を置く
//   /fonts        …… 日本語 TTF（フラッシュに焼かない場合）
//   /.paperdiy    …… アプリが作る作業ディレクトリ
inline constexpr const char* kBooksDir = "/books";
inline constexpr const char* kFontDir = "/fonts";
inline constexpr const char* kAppDir = "/.paperdiy";
inline constexpr const char* kCacheDir = "/.paperdiy/cache";
inline constexpr const char* kStateDir = "/.paperdiy/state";
inline constexpr const char* kSettingsPath = "/.paperdiy/settings.bin";
inline constexpr const char* kLibraryPath = "/.paperdiy/library.bin";
inline constexpr const char* kRecentPath = "/.paperdiy/recent.bin";
inline constexpr const char* kWifiPath = "/.paperdiy/wifi.bin";
inline constexpr const char* kOpdsPath = "/.paperdiy/opds.bin";

// フラッシュのフォント置き場（partitions.csv と合わせる）
inline constexpr const char* kFontPartitionName = "font";
inline constexpr uint8_t kFontPartitionSubtype = 0x40;

// 画面（PaperDIY のパネルは 960x540 横長。縦持ちで使うので論理 540x960）
inline constexpr int16_t kPanelWidth = 960;
inline constexpr int16_t kPanelHeight = 540;
inline constexpr int16_t kPageWidth = 540;
inline constexpr int16_t kPageHeight = 960;

// 設定で選べる値
inline constexpr uint16_t kFontSizes[] = {18, 20, 22, 24, 27, 30, 34};
inline constexpr uint8_t kFontSizeCount = sizeof(kFontSizes) / sizeof(kFontSizes[0]);
inline constexpr uint16_t kLineGaps[] = {150, 165, 180, 200, 220};
inline constexpr uint8_t kLineGapCount = sizeof(kLineGaps) / sizeof(kLineGaps[0]);
inline constexpr int16_t kMargins[] = {16, 24, 32, 44};
inline constexpr uint8_t kMarginCount = sizeof(kMargins) / sizeof(kMargins[0]);

// 1 つのフォルダから読み込む項目の数。これを超えるぶんは一覧に出さない
// （深い階層に潜っても、いちどに持つのはこの数だけ）。
inline constexpr uint16_t kMaxEntries = 256;

// 「最近読んだ本」に残す数。
inline constexpr uint8_t kMaxRecent = 10;

// 表紙サムネイルの高さ（幅は 0.6 倍）。1 冊ぶんの画素は
//   ((高さ*0.6 + 31)/32)*4 × 高さ バイト。
inline constexpr uint16_t kCoverHeight = 126;

// 1 冊あたりのしおりの数。超えたら一番古いものから捨てる。
inline constexpr uint16_t kMaxBookmarks = 16;

// 自動ページ送りの速さ（1 分あたりのページ数）。0 番目は「送らない」。
inline constexpr uint8_t kAutoTurnRates[] = {0, 1, 3, 6, 12};
inline constexpr uint8_t kAutoTurnCount = sizeof(kAutoTurnRates) / sizeof(kAutoTurnRates[0]);

// 読了時に出す「次の本」の数。
inline constexpr uint8_t kNextBookCount = 3;

}  // namespace paperdiy
