# Badger の qrcode モジュール互換。

import paperdiy as _pd


class QRCode:
    def __init__(self):
        self._size = 0
        self._bits = b""

    def set_text(self, text):
        self._size, self._bits = _pd.qr_matrix(text)

    def get_size(self):
        return self._size, self._size

    def get_module(self, x, y):
        if not (0 <= x < self._size and 0 <= y < self._size):
            return False
        row = (self._size + 7) // 8
        return bool(self._bits[y * row + x // 8] & (0x80 >> (x % 8)))
