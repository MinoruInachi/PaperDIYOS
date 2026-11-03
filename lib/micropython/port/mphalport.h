#pragma once

// virtpin に依存しないように定義だけ置く。
#define mp_hal_pin_obj_t void *

void mp_hal_set_interrupt_char(int c);
