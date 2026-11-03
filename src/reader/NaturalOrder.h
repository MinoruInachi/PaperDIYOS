#pragma once

// 数字を数として比べる並び順。「2 巻」が「10 巻」より前に来る。
// 大文字小文字は区別しない。UTF-8 はバイトの順で比べる。
//
// Arduino にも SDK にも依存しない。ホストのテストでそのまま回せる。

namespace paperdiy {

bool naturalLess(const char* a, const char* b);

}  // namespace paperdiy
