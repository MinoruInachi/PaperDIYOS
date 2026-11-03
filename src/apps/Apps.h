#pragma once

// アプリを増やすときは、apps/ にファイルを足して App を継承し、
// ここへ宣言を、Apps.cpp の一覧へ 1 行を足す。

#include "os/Os.h"

namespace pdos {

App& badgeApp();
App& clockApp();
App& ebookApp();
App& fontsApp();
App& helpApp();
App& imageApp();
App& infoApp();
App& listApp();
App& netInfoApp();
App& newsApp();
App& qrgenApp();
App& weatherApp();
App& transferApp();
App& settingsApp();

}  // namespace pdos
