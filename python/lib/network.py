# network 互換の最小限。接続先は設定アプリ（または /config/wifi.txt）で決める。

import paperdiy as _pd

STA_IF = 0
AP_IF = 1
STAT_IDLE = 0
STAT_CONNECTING = 1
STAT_GOT_IP = 3


class WLAN:
    def __init__(self, interface=STA_IF):
        pass

    def active(self, on=None):
        if on is False:
            _pd.wifi_stop()
        return _pd.wifi_online()

    def connect(self, ssid=None, key=None):
        _pd.wifi_connect()

    def disconnect(self):
        _pd.wifi_stop()

    def isconnected(self):
        return _pd.wifi_online()

    def status(self, param=None):
        return STAT_GOT_IP if _pd.wifi_online() else STAT_IDLE

    def ifconfig(self):
        return (_pd.wifi_ip(), "", "", "")
