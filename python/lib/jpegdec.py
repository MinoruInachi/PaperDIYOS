# jpegdec 互換。表示は OS の画像デコーダに任せる（16 階調）。

import paperdiy as _pd

JPEG_SCALE_FULL = 1
JPEG_SCALE_HALF = 2
JPEG_SCALE_QUARTER = 4
JPEG_SCALE_EIGHTH = 8


class JPEG:
    def __init__(self, display=None):
        self._path = None

    def open_file(self, path):
        path = path.strip()
        _pd.image_size(path)  # 無ければここで OSError
        self._path = path

    def get_width(self):
        return _pd.image_size(self._path)[0]

    def get_height(self):
        return _pd.image_size(self._path)[1]

    def decode(self, x=0, y=0, scale=JPEG_SCALE_FULL, dither=True):
        w, h = _pd.image_size(self._path)
        _pd.image(self._path, x, y, w // scale, h // scale)
