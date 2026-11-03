#pragma once

// USB シリアルのコンソール。tools/pdiy.py と話す。
//
// 実機のボタンを押さずに操作でき、画面の中身を取り出せるので、確かめ物に使う。
// SD へのファイルの出し入れもここからできる（Badger の Thonny に当たる）。

namespace pdos {
namespace console {

void poll();

}  // namespace console
}  // namespace pdos
