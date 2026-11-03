# Badger 2040 互換の API。Badger OS のスクリプトがそのまま読める名前を揃えてある。
#
# 違うところ:
#   画面は 540x960（WIDTH / HEIGHT）。16 階調がそのまま出る（ディザではない）。
#   ボタンは 2 個なので押し方で割り当てる。
#     A を押す      BUTTON_UP
#     B を押す      BUTTON_DOWN
#     B を長く押す  BUTTON_B
#     A を長く押す  戻る（スクリプトを止めてランチャーへ。他のアプリと同じ操作）
#     A と B を同時 ランチャーへ（Badger の A+C）
#     BUTTON_A は paperdiy.capture_back(True) のあとの A 長押し。それ以外では押せない
#     BUTTON_C と BUTTON_USER は無い（常に False）
#   halt() はボタンを待つ。電池で長く放っておくと眠り、起きたらスクリプトの
#   先頭からやり直す（Badger と同じ）。
#   文字は日本語の TrueType フォントで描く。set_font の指定は大きさの目安にだけ使う。

import time

import paperdiy as _pd

BUTTON_DOWN = 11
BUTTON_A = 12
BUTTON_B = 13
BUTTON_C = 14
BUTTON_UP = 15
BUTTON_USER = None

BUTTON_MASK = 0b11111 << 11

SYSTEM_VERY_SLOW = 0
SYSTEM_SLOW = 1
SYSTEM_NORMAL = 2
SYSTEM_FAST = 3
SYSTEM_TURBO = 4

UPDATE_NORMAL = 0
UPDATE_MEDIUM = 1
UPDATE_FAST = 2
UPDATE_TURBO = 3

WIDTH = _pd.WIDTH
HEIGHT = _pd.HEIGHT

_KEY_TO_BUTTON = {
    _pd.KEY_A: BUTTON_UP,
    _pd.KEY_B: BUTTON_DOWN,
    _pd.KEY_LONG_A: BUTTON_A,
    _pd.KEY_LONG_B: BUTTON_B,
}

# Hershey 系の名前は y が文字の中央、ビットマップ系は上端。
_BITMAP_FONTS = {"bitmap6": 6, "bitmap8": 8, "bitmap14_outline": 14}



class _Button:
    # Badger の BUTTONS[...] に当たるもの。ピンの割り込みは使えないので、
    # 読むと常に離した状態。ボタンは pressed() で読むこと。
    def value(self):
        return 0

    def irq(self, trigger=None, handler=None):
        pass


BUTTONS = {b: _Button() for b in (BUTTON_DOWN, BUTTON_A, BUTTON_B, BUTTON_C, BUTTON_UP)}

_wake_button = _KEY_TO_BUTTON.get(_pd.wake_key())
_wake_seen = False


def is_wireless():
    return True


def woken_by_rtc():
    return False


def woken_by_button():
    return _wake_button is not None


def pressed_to_wake(button):
    return _wake_button == button


def reset_pressed_to_wake():
    global _wake_button
    _wake_button = None


def pressed_to_wake_get_once(button):
    global _wake_seen
    if _wake_button == button and not _wake_seen:
        _wake_seen = True
        return True
    return False


def system_speed(speed):
    pass


def turn_on():
    pass


def turn_off():
    _pd.wait_key()


def pico_rtc_to_pcf():
    pass


def pcf_to_pico_rtc():
    return True


def sleep_for(minutes):
    # USB のあいだは眠らず、ボタンか時間で戻る。電池なら deep sleep。
    if _pd.battery()[2]:
        _pd.wait_key(int(minutes * 60000))
    else:
        _pd.deep_sleep(int(minutes * 60))


class Badger2040:
    def __init__(self):
        # jpegdec.JPEG(display.display) のように PicoGraphics として渡される。
        self.display = self
        self._pen = 0
        self._font = "bitmap8"
        self._speed = UPDATE_NORMAL
        self._pending = set()
        _pd.set_pen(0)
        if _wake_button is not None:
            self._pending.add(_wake_button)

    # --- 描画 -----------------------------------------------------------------

    def get_bounds(self):
        return WIDTH, HEIGHT

    def set_pen(self, pen):
        self._pen = pen
        _pd.set_pen(pen)

    def create_pen(self, r, g, b):
        return (r * 299 + g * 587 + b * 114) // 17000

    def clear(self):
        _pd.clear(self._pen)

    def pixel(self, x, y):
        _pd.pixel(x, y)

    def pixel_span(self, x, y, length):
        _pd.rect(x, y, length, 1)

    def rectangle(self, x, y, w, h):
        _pd.rect(x, y, w, h)

    def line(self, x1, y1, x2, y2, thickness=1):
        _pd.line(x1, y1, x2, y2, thickness)

    def circle(self, x, y, r):
        _pd.circle(x, y, r)

    def set_font(self, font):
        self._font = font

    def set_thickness(self, thickness):
        pass

    def thickness(self, thickness):
        pass

    def _em(self, scale):
        size = _BITMAP_FONTS.get(self._font)
        em = size * scale if size else 30 * scale
        return max(8, int(em))

    def _baseline(self, y, em):
        return y + em * 85 // 100 if self._font in _BITMAP_FONTS else y + em * 35 // 100

    def text(self, text, x, y, wordwrap=None, scale=2, angle=0, spacing=1, fixed_width=False):
        em = self._em(scale)
        base = self._baseline(y, em)
        # wordwrap が 0 のときは折り返さない（Badger の clock.py がこう呼ぶ）。
        if wordwrap and _pd.text_width(text, em) > wordwrap:
            _pd.text_wrap(text, x, base, wordwrap, em, em * 5 // 4)
        else:
            _pd.text(text, x, base, em)

    def measure_text(self, text, scale=2, spacing=1, fixed_width=False):
        return _pd.text_width(text, self._em(scale))

    def image(self, data, w, h, x, y):
        for oy in range(h):
            row = data[oy]
            for ox in range(w):
                if row & 0b1 == 0:
                    _pd.pixel(x + ox, y + oy)
                row >>= 1

    def icon(self, data, index, data_w, icon_size, x, y):
        s_x = (index * icon_size) % data_w
        s_y = int((index * icon_size) / data_w)
        for o_y in range(icon_size):
            for o_x in range(icon_size):
                o = ((o_y + s_y) * data_w) + (o_x + s_x)
                if data[o >> 3] & (0b10000000 >> (o & 0b111)):
                    _pd.pixel(x + o_x, y + o_y)

    # --- 更新 -----------------------------------------------------------------

    def update(self):
        _pd.update(self._speed)

    def partial_update(self, x, y, w, h):
        # パネルは変わった画素だけを動かすので、全体を送っても同じ結果になる。
        _pd.update(self._speed)

    def set_update_speed(self, speed):
        self._speed = speed

    def update_speed(self, speed):
        self._speed = speed

    def led(self, brightness):
        pass

    def invert(self, invert):
        raise RuntimeError("Display invert not supported")

    # --- ボタン ---------------------------------------------------------------

    def _drain(self):
        while True:
            k = _pd.key()
            if k == _pd.KEY_NONE:
                return
            self._pending.add(_KEY_TO_BUTTON.get(k))

    def pressed(self, button):
        self._drain()
        if button in self._pending:
            self._pending.discard(button)
            return True
        return False

    def pressed_any(self):
        self._drain()
        return len(self._pending) > 0

    def keepalive(self):
        pass

    def halt(self):
        self._drain()
        if not self._pending:
            k = _pd.wait_key()
            self._pending.add(_KEY_TO_BUTTON.get(k))

    # --- 無線 -----------------------------------------------------------------

    def status_handler(self, mode, status, ip):
        print(mode, status, ip)

    def isconnected(self):
        return _pd.wifi_online()

    def ip_address(self):
        return _pd.wifi_ip()

    def connect(self, **args):
        status_handler = args.get("status_handler", self.status_handler)
        if status_handler:
            status_handler(None, None, "")
        try:
            _pd.wifi_connect(args.get("timeout", 15))
        except OSError:
            if status_handler:
                status_handler(None, False, "")
            raise
        if status_handler:
            status_handler(None, True, _pd.wifi_ip())
