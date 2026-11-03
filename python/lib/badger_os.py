# Badger OS の badger_os.py に当たる。状態の保存先も同じ /state/*.json。

import gc
import json
import os

import badger2040
import paperdiy as _pd


def get_battery_level():
    return _pd.battery()[0] / 1000


def get_disk_usage():
    f_bsize, f_frsize, f_blocks, f_bfree, _, _, _, _, _, f_namemax = os.statvfs("/")
    f_total_size = f_frsize * f_blocks
    f_total_free = f_bsize * f_bfree
    f_total_used = f_total_size - f_total_free
    if f_total_size == 0:
        return 0, 0, 0
    f_used = 100 / f_total_size * f_total_used
    f_free = 100 / f_total_size * f_total_free
    return f_total_size, f_used, f_free


def state_delete(app):
    try:
        os.remove("/state/{}.json".format(app))
    except OSError:
        pass


def state_save(app, data):
    try:
        with open("/state/{}.json".format(app), "w") as f:
            f.write(json.dumps(data))
            f.flush()
    except OSError:
        try:
            os.stat("/state")
        except OSError:
            os.mkdir("/state")
            state_save(app, data)


def state_modify(app, data):
    state = {}
    state_load(app, state)
    state.update(data)
    state_save(app, state)


def state_load(app, defaults):
    try:
        with open("/state/{}.json".format(app), "r") as f:
            data = json.loads(f.read())
        if type(data) is dict:
            defaults.update(data)
            return True
    except (OSError, ValueError):
        pass
    state_save(app, defaults)
    return False


def launch(file):
    gc.collect()
    if file.endswith(".py"):
        _pd.launch(file)
    elif "/" in file:
        _pd.launch(file + ".py")
    else:
        _pd.launch(file)


def warning(display, message, width=badger2040.WIDTH - 40, height=badger2040.HEIGHT // 3, line_spacing=44, text_size=0.8):
    print(message)
    if display is None:
        display = badger2040.Badger2040()

    x = (badger2040.WIDTH - width) // 2
    y = (badger2040.HEIGHT - height) // 2
    display.set_pen(12)
    display.rectangle(x, y, width, height)
    display.set_pen(15)
    display.rectangle(x + 10, y + 10, width - 20, height - 20)
    display.set_pen(0)
    _pd.text_wrap(message, x + 24, y + 60, width - 48, 28, line_spacing)
    display.update()
