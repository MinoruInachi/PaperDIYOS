# 本体の様子を出す。paperdiy モジュール（本体の直接の窓口）の見本。
#   A / B: 更新  A 長押し: 戻る

import gc
import sys
import time

import paperdiy as pd

JA = pd.language() == "ja"


def row(y, label, value):
    pd.text(label, 24, y, 26)
    pd.text(value, 516 - pd.text_width(value, 26), y, 26)
    pd.line(24, y + 14, 516, y + 14)
    return y + 64


def render():
    pd.clear()
    pd.set_pen(0)
    pd.text("System" if not JA else "システム", 24, 60, 40)
    mv, pct, ext, chg = pd.battery()
    t = time.localtime()
    y = 140
    y = row(y, "Python", ".".join(str(v) for v in sys.implementation.version[:3]))
    y = row(y, "Heap" if not JA else "メモリの空き", "{} KB".format(gc.mem_free() // 1024))
    y = row(y, "Battery" if not JA else "電池", "{:.2f} V  {}%".format(mv / 1000, pct))
    y = row(y, "USB", "yes" if ext else "no")
    y = row(y, "Charging" if not JA else "充電", "yes" if chg else "no")
    y = row(y, "Date" if not JA else "日付", "{}-{:02}-{:02}".format(t[0], t[1], t[2]))
    y = row(y, "Time" if not JA else "時刻", "{:02}:{:02}:{:02}".format(t[3], t[4], t[5]))
    pd.text("A/B: update  Hold A: back" if not JA else "A/B: 更新  A長押し: 戻る", 24, 930, 22)
    pd.update(pd.UPDATE_TEXT)


while True:
    render()
    pd.wait_key()
