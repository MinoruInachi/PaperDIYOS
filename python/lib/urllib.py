# from urllib import urequest の互換（Badger の news.py が使う）。

import io

import urequests


class urequest:
    @staticmethod
    def urlopen(url, data=None, method="GET"):
        return io.BytesIO(urequests.get(url).content)
