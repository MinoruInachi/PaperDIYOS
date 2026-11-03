// PaperDIY OS — M5Stack PaperDIY 向けの Badger OS 風環境。
//
// 本体は os/Os.cpp、アプリは apps/ の下。

#include <Arduino.h>

#include "os/Os.h"

// 本文のレイアウトと画像の展開、MicroPython はスタックを深く使う。
// MicroPython のスタック検査の値（lib/micropython/port/pd_port.c）より大きくすること。
SET_LOOP_TASK_STACK_SIZE(48 * 1024);

void setup() { pdos::os.begin(); }

void loop() {
  pdos::os.loop();
  delay(1);
}
