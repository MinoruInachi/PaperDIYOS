#!/usr/bin/env python3
"""USB シリアル越しに PaperDIY OS を操作する（Badger の Thonny に当たる）。

  pdiy.py info                    状態を見る
  pdiy.py key a|b|A|B|ab          ボタンを押す（大文字は長押し、ab は同時押し）
  pdiy.py shot out.png            画面の中身を PNG に落とす（pip install pillow）
  pdiy.py app ebook               アプリを切り替える
  pdiy.py ls /books               SD の中を見る
  pdiy.py push 本.epub /books/本.epub   SD へファイルを送る
  pdiy.py pull /books/本.epub 本.epub   SD からファイルを取り出す
  pdiy.py rm /books/本.epub       SD のファイルを消す
  pdiy.py reboot / sleep          再起動 / deep sleep
  pdiy.py repl                    MicroPython の REPL（Ctrl-] で抜ける）

mpremote や Thonny でもつなげる（MicroPython として見える）。

ポートは自動で探す。--port で指定もできる。
"""
import argparse
import base64
import glob
import os
import sys
import time

import serial


def find_port() -> str:
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("PaperDIY が見つかりません（USB をつないでください）")
    return ports[0]


def open_port(name: str) -> serial.Serial:
    # DTR/RTS は既定のまま開く。明示的に下げると、その遷移で本体がリセットされる。
    s = serial.Serial(name, 115200, timeout=2)
    time.sleep(0.1)
    s.reset_input_buffer()
    # REPL に入ったままなら閉じる。コンソールは空行を黙って捨てる。
    s.write(b"\r\n")
    s.flush()
    time.sleep(0.15)
    reply = s.read(s.in_waiting or 0)
    if b">>>" in reply or b"raw REPL" in reply or reply.endswith(b">"):
        s.write(b"\x02\r\nimport paperdiy;paperdiy.leave_repl()\r\n")
        s.flush()
        # アプリが立ち上がり直すまで待つ。途中で送ると REPL に食われる。
        time.sleep(1.5)
        s.reset_input_buffer()
    return s


def send(s: serial.Serial, line: str) -> None:
    s.write(line.encode("utf-8") + b"\n")
    s.flush()


def read_reply(s: serial.Serial, timeout: float = 30.0) -> list:
    """OK か ERR の行が来るまで読む。ログの行もそのまま返す。"""
    out = []
    end = time.time() + timeout
    while time.time() < end:
        raw = s.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").rstrip()
        out.append(line)
        if line == "OK" or line.startswith("ERR"):
            return out
    sys.exit("応答がありません: " + "\n".join(out))


def cmd_simple(s: serial.Serial, line: str, timeout: float = 30.0) -> int:
    send(s, line)
    reply = read_reply(s, timeout)
    for r in reply:
        print(r)
    return 1 if reply[-1].startswith("ERR") else 0


def cmd_shot(s: serial.Serial, path: str) -> int:
    from PIL import Image

    # 組版中のログが混ざると転送が壊れるので、静かになるのを待つ。
    while s.readline():
        pass
    send(s, "shot")
    end = time.time() + 30
    while time.time() < end:
        line = s.readline().decode("utf-8", "replace").strip()
        if line.startswith("SHOT"):
            break
    else:
        sys.exit("画面を受け取れません")
    _, w, h = line.split()
    w, h = int(w), int(h)
    img = Image.new("L", (w, h))
    px = img.load()
    for y in range(h):
        row = base64.b64decode(s.readline().strip())
        for i, b in enumerate(row):
            px[i * 2, y] = (b >> 4) * 17
            px[i * 2 + 1, y] = (b & 0x0F) * 17
    if s.readline().strip() != b"END":
        sys.exit("画面の転送が途中で切れました")
    img.save(path)
    print(path)
    return 0


def cmd_push(s: serial.Serial, local: str, remote: str) -> int:
    size = os.path.getsize(local)
    send(s, f"put {remote} {size}")
    while True:
        line = s.readline().decode("utf-8", "replace").strip()
        if line == "READY":
            break
        if line.startswith("ERR"):
            sys.exit(line)
        if not line:
            sys.exit("応答がありません")
    sent = 0
    with open(local, "rb") as f:
        while True:
            chunk = f.read(360)
            if not chunk:
                break
            s.write(base64.b64encode(chunk) + b"\n")
            s.flush()
            end = time.time() + 15
            while s.readline().strip() != b"+":
                if time.time() > end:
                    sys.exit("応答がありません（書き込みが止まりました）")
            sent += len(chunk)
            print(f"\r{sent}/{size}", end="", flush=True)
    print()
    return cmd_read_ok(s)


def cmd_pull(s: serial.Serial, remote: str, local: str) -> int:
    send(s, f"get {remote}")
    while True:
        line = s.readline().decode("utf-8", "replace").strip()
        if line.startswith("SIZE "):
            size = int(line.split()[1])
            break
        if line.startswith("ERR"):
            sys.exit(line)
        if not line:
            sys.exit("応答がありません")
    got = 0
    with open(local, "wb") as f:
        while got < size:
            data = base64.b64decode(s.readline().strip())
            if not data:
                sys.exit("転送が途中で切れました")
            f.write(data)
            got += len(data)
    return cmd_read_ok(s)


def cmd_repl(s: serial.Serial) -> int:
    import select
    import termios
    import tty

    print("REPL（Ctrl-] で抜ける。本体は A+B か USB を閉じると元の画面へ戻る）")
    s.write(b"\x03")
    s.flush()
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        while True:
            ready, _, _ = select.select([fd, s.fileno()], [], [])
            if fd in ready:
                data = os.read(fd, 256)
                if b"\x1d" in data:
                    return 0
                s.write(data)
                s.flush()
            if s.fileno() in ready:
                sys.stdout.buffer.write(s.read(s.in_waiting or 1))
                sys.stdout.buffer.flush()
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)
        print()


def cmd_read_ok(s: serial.Serial) -> int:
    reply = read_reply(s)
    print(reply[-1])
    return 1 if reply[-1].startswith("ERR") else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    s = open_port(a.port or find_port())
    if a.cmd == "shot":
        return cmd_shot(s, a.args[0] if a.args else "screen.png")
    if a.cmd == "push":
        return cmd_push(s, a.args[0], a.args[1])
    if a.cmd == "repl":
        return cmd_repl(s)
    if a.cmd == "pull":
        return cmd_pull(s, a.args[0], a.args[1])
    if a.cmd in ("info", "key", "ls", "rm", "app", "sleep", "reboot", "lsleep"):
        return cmd_simple(s, " ".join([a.cmd] + a.args))
    ap.error("知らないコマンドです")
    return 2


if __name__ == "__main__":
    sys.exit(main())
