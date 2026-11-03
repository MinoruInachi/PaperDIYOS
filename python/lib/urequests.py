# urequests 互換の GET だけ。TLS と証明書の検証は OS が受け持つ。

import json as _json

import paperdiy as _pd


class Response:
    def __init__(self, status, content):
        self.status_code = status
        self.reason = b"OK" if status == 200 else b""
        self.content = content

    @property
    def text(self):
        return str(self.content, "utf-8")

    def json(self):
        return _json.loads(self.content)

    def close(self):
        pass


def get(url, **kw):
    if not _pd.wifi_online():
        _pd.wifi_connect()
    status, content = _pd.http_get(url)
    return Response(status, content)


def request(method, url, **kw):
    if method != "GET":
        raise NotImplementedError("only GET is supported")
    return get(url, **kw)
