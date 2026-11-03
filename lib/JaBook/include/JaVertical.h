#pragma once

// JaBook — 縦組みレイアウトエンジン
//
// FreeInkBook の ChapterLayout を縦組みに置き換えるレイヤ。入出力の型は
// FreeInkBook と共有する：
//
//   - 入力  : BookSource + ZipCatalog + ZipEntry（EPUB の章）／プレーンテキスト
//   - 出力  : book::Page を book::PageSink へ（= PageCacheWriter にそのまま流せる）
//
// つまりページキャッシュ・しおり・進捗（charStart）・目次ジャンプは横組みと
// 同じ仕組みがそのまま効く。違うのはページの中身の座標系だけで、それは
// JaVerticalRenderer が解釈する。
//
// Arduino / ESP-IDF 非依存の freestanding C++17。

#include <stdint.h>

#include <BookArena.h>
#include <BookFont.h>
#include <BookStorage.h>
#include <BookTypes.h>
#include <epub/ZipCatalog.h>
#include <layout/ChapterLayout.h>

namespace jabook {

namespace fb = freeink::book;

// PageTextRun::styleFlags の空きビット（FreeInkBook の StyleFlags は bit0..4）。
enum VertFlags : uint8_t {
  // グリフを時計回り 90 度回して組む。回転 run の座標の意味は下記のとおり。
  VertRotated = 1u << 5,
  // ルビ（本文列の右脇に組まれた小書き）。描画は通常 run と同じ。
  VertRuby = 1u << 6,
};

// ---------------------------------------------------------------------------
// PageTextRun の座標の約束（縦組み）
//
//   回転なし（VertRotated 立たず）:
//     x         = ペン位置（左端）
//     baselineY = ベースライン
//     …横組みとまったく同じ。全角文字も、縦中横も、ルビもこれ。
//
//   回転あり（VertRotated 立つ）:
//     x         = 文字枠の左端 X（枠幅は sizePx）
//     baselineY = 進行方向の開始 Y（枠の上端）
//     …レンダラが 90 度回した位置にグリフを置く。run 内の文字は
//       上から下へ、各グリフの横組み advance ぶん進む。
// ---------------------------------------------------------------------------

struct VerticalLayoutParams {
  // 論理ページ（縦長）。PaperDIY なら 540x960。
  int16_t pageWidth = 480;
  int16_t pageHeight = 800;
  int16_t marginTop = 40;
  int16_t marginBottom = 40;
  int16_t marginLeft = 28;
  int16_t marginRight = 28;

  // 全角 1 文字の送り（＝本文の字面サイズ）。
  uint16_t baseSizePx = 24;
  // 行送り = baseSizePx * lineGapPct / 100。ルビを出すなら 170 以上が目安。
  uint16_t lineGapPct = 180;
  // 段落間の空き = baseSizePx * paragraphSpacingPct / 100（列方向の空き）。
  uint16_t paragraphSpacingPct = 0;
  // 段落先頭の字下げ（全角何文字ぶんか、10 倍値）。10 = 1 字下げ。
  uint16_t firstLineIndentTenths = 10;

  // 見出しの拡大率と、見出し前後の空き（全角何文字ぶんかの 10 倍値）。
  uint16_t headingScalePct = 130;
  uint16_t headingSpaceTenths = 10;

  // 全角枠の上端からベースラインまでの比率（%）。和文フォントの標準は 88。
  uint16_t emAscentPct = 88;

  // ルビを組む。
  bool ruby = true;
  uint16_t rubySizePct = 50;  // 本文に対するルビの大きさ

  // 半角数字 2 桁を横に並べて 1 字ぶんに収める（縦中横）。
  bool tateChuYoko = true;

  // JLREQ 厳格版の行頭禁則（小書きのかな・長音符も行頭に置かない）。
  bool strictKinsoku = false;
  // 行末に来た句読点を版面の外へぶら下げる（追い出しより字間が乱れない）。
  bool hangingPunctuation = true;

  // 小書きのかなを右上へ寄せる量（全角に対する %）。0 で無調整。
  uint16_t smallKanaShiftPct = 8;

  fb::BookFont* font = nullptr;
};

// レイアウトに影響する値すべてのハッシュ。横組みの
// freeink::book::layoutGenerationHash と同じ役割で、値域も分けてあるので
// 横組みキャッシュと取り違えない。
uint32_t verticalLayoutGenerationHash(const VerticalLayoutParams& params,
                                      uint32_t fontFingerprint);

class VerticalLayout {
 public:
  // EPUB の章（XHTML）を縦組みにしてページを sink へ流す。作業メモリは
  // すべて scratch から取り、戻る前に解放する。
  static fb::BookStatus layout(fb::BookSource& source, const fb::ZipCatalog& zip,
                               const fb::ZipEntry& entry, const char* chapterHref,
                               const VerticalLayoutParams& params, fb::Arena& scratch,
                               fb::PageSink& sink, uint32_t* pageCountOut = nullptr,
                               uint32_t* totalCharsOut = nullptr);

  // プレーンテキスト（.txt / 青空文庫形式のルビ付きテキスト）。空行で段落を
  // 区切り、単独の改行は改行として扱う（和文では行送りを保つほうが自然）。
  // aozoraRuby = true で ｜漢字《かんじ》 と 漢字《かんじ》 を解釈する。
  static fb::BookStatus layoutPlainText(fb::BookSource& source,
                                        const VerticalLayoutParams& params, fb::Arena& scratch,
                                        fb::PageSink& sink, bool aozoraRuby = true,
                                        uint32_t* pageCountOut = nullptr,
                                        uint32_t* totalCharsOut = nullptr);
};

}  // namespace jabook
