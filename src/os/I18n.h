#pragma once

// 画面の言語。設定アプリで切り替え、NVS に残す。
//
//   TR("本棚", "Library")
//
// 文字列を呼び出しの場所に並べて書く。翻訳表を別に持たないぶん、片方だけ
// 直し忘れることが少ない。本の中身と USB のログは対象にしない。

#include <stdint.h>

namespace pdos {
namespace i18n {

enum class Lang : uint8_t { Ja = 0, En = 1 };

Lang lang();
void set(Lang l);
void load();

inline const char* tr(const char* ja, const char* en) { return lang() == Lang::Ja ? ja : en; }

}  // namespace i18n
}  // namespace pdos

#define TR(ja, en) ::pdos::i18n::tr(ja, en)
