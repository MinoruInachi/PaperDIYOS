#pragma once

// Os から MicroPython の割り込みに触るための小さな窓口（実装は PyBridge.cpp）。

namespace pdos {

// 走っている Python に KeyboardInterrupt を起こす。
void pyInterrupt();
// コードの実行中だけ起こす（REPL のプロンプトで待っているときは何もしない）。
void pyInterruptIfRunning();
// いまの割り込み文字（無効なら -1）。
int pyInterruptChar();
void pythonRunRepl();

}  // namespace pdos
