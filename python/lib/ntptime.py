# ntptime 互換。サーバーとタイムゾーンは設定アプリで決める。

import paperdiy as _pd


def settime():
    if not _pd.ntp_sync():
        raise OSError("ntp failed")
