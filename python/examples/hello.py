# 最初の一歩。Badger の書き方そのままで、ボタンを押した回数を数える。
#   A: 減らす  B: 増やす  A 長押し: 戻る
# 値は /state/hello.json に残るので、眠っても電源を切っても消えない。

import badger2040
import badger_os

display = badger2040.Badger2040()
state = {"count": 0}
badger_os.state_load("hello", state)


def render():
    display.set_pen(15)
    display.clear()
    display.set_pen(0)
    display.rectangle(0, 0, badger2040.WIDTH, 60)
    display.set_pen(15)
    display.set_font("sans")
    display.text("こんにちは MicroPython", 20, 30, scale=0.9)
    display.set_pen(0)
    text = str(state["count"])
    display.text(text, (badger2040.WIDTH - display.measure_text(text, 5)) // 2, 420, scale=5)
    display.text("A -1   B +1   A長押し 戻る", 40, 880, scale=0.9)
    display.update()


display.set_update_speed(badger2040.UPDATE_MEDIUM)
render()
display.set_update_speed(badger2040.UPDATE_FAST)

while True:
    display.keepalive()
    if display.pressed(badger2040.BUTTON_UP):
        state["count"] -= 1
    elif display.pressed(badger2040.BUTTON_DOWN):
        state["count"] += 1
    else:
        display.halt()
        continue
    badger_os.state_save("hello", state)
    render()
