# ごく小さな machine。ピンは OS が持っているので触らせない。

import time

import paperdiy as _pd

PWRON_RESET = 1
HARD_RESET = 2
WDT_RESET = 3
DEEPSLEEP_RESET = 4
SOFT_RESET = 5


def reset():
    _pd.reset()


def soft_reset():
    _pd.reset()


def reset_cause():
    return DEEPSLEEP_RESET if _pd.wake_key() else PWRON_RESET


def idle():
    time.sleep_ms(1)


def freq():
    return 240000000


def unique_id():
    return b"paperdiy"


def deepsleep(ms=0):
    _pd.deep_sleep(ms // 1000)


class Pin:
    # ピンは OS が持っているので、作れても触れない。読むと常に 0、割り込みは来ない。
    # ボタンは badger2040 の pressed() で読むこと。
    IN = 0
    OUT = 1
    PULL_UP = 1
    PULL_DOWN = 2
    IRQ_RISING = 1
    IRQ_FALLING = 2

    def __init__(self, *args, **kwargs):
        pass

    def init(self, *args, **kwargs):
        pass

    def value(self, v=None):
        return 0

    def on(self):
        pass

    def off(self):
        pass

    def irq(self, *args, **kwargs):
        pass


class RTC:
    def datetime(self, t=None):
        if t is None:
            y, mo, d, h, mi, s, wd, _ = time.localtime()
            return (y, mo, d, wd, h, mi, s, 0)
        _pd.set_time(time.mktime((t[0], t[1], t[2], t[4], t[5], t[6], t[3], 0)))
