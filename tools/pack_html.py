#!/usr/bin/env python3
"""HTML を gzip で縮めて C の配列にする。

ファームウェアはこの配列をそのまま返し、ヘッダに Content-Encoding: gzip を
付ける。ブラウザ側で展開されるので、機体は展開しない。

    python3 tools/pack_html.py src/html/files.html src/html/FilesPage.h FilesPage

brotli は使わない。Firefox は暗号化されていない接続だと brotli を受け取らず、
この Web UI はまさにその「暗号化されていない接続」で出している。
"""

import gzip
import re
import sys


def minify(html: str) -> str:
    """コメントを落として、タグのあいだの空白を詰める。

    <script> と <style> の中身は触らない（JavaScript は改行に意味がある）。
    """
    parts = re.split(r"(<script\b[^>]*>.*?</script>|<style\b[^>]*>.*?</style>)",
                     html, flags=re.S | re.I)
    out = []
    for i, part in enumerate(parts):
        if i % 2 == 1:  # script / style はそのまま
            out.append(part)
            continue
        part = re.sub(r"<!--(?!\[if).*?-->", "", part, flags=re.S)
        part = re.sub(r">\s+<", "><", part)
        part = re.sub(r"\s{2,}", " ", part)
        out.append(part)
    return "".join(out).strip()


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 1
    src, dst, name = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(src, encoding="utf-8") as f:
        html = f.read()

    packed = minify(html).encode("utf-8")
    # mtime=0 で、中身が同じなら毎回同じ出力になるようにする。
    blob = gzip.compress(packed, compresslevel=9, mtime=0)

    lines = []
    for i in range(0, len(blob), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in blob[i:i + 16])
        lines.append("    " + chunk + ",")

    with open(dst, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        f.write(f"// {src} から作った。手で直さないこと。\n")
        f.write(f"//   python3 tools/pack_html.py {src} {dst} {name}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"inline constexpr uint8_t {name}Gzip[] = {{\n")
        f.write("\n".join(lines))
        f.write("\n};\n\n")
        f.write(f"inline constexpr unsigned {name}GzipSize = {len(blob)};\n")

    raw = len(html.encode("utf-8"))
    print(f"{src}: {raw} → 縮めて {len(packed)} → gzip {len(blob)} バイト")
    return 0


if __name__ == "__main__":
    sys.exit(main())
