# pngdec 互換。表示は OS の画像デコーダに任せる（16 階調）。

import paperdiy as _pd

PNG_DITHER = 0
PNG_COPY = 1
PNG_POSTERIZE = 2


class PNG:
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

    def decode(self, x=0, y=0, source=None, scale=(1, 1), mode=PNG_DITHER, rotate=0):
        w, h = _pd.image_size(self._path)
        sx = scale[0] if isinstance(scale, tuple) else scale
        _pd.image(self._path, x, y, w * sx, h * sx)
