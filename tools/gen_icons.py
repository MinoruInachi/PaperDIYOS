#!/usr/bin/env python3
"""Lucide の SVG から src/os/Icons.h を作る（1bit、MSB 先頭、1 = 黒）。

  python3 -m venv .venv && .venv/bin/pip install pymupdf
  git clone --depth 1 https://github.com/lucide-icons/lucide.git /tmp/lucide
  .venv/bin/python tools/gen_icons.py /tmp/lucide/icons src/os/Icons.h

生成物はリポジトリに入っているので、アイコンを足すときだけ走らせればよい。
"""
import re
import sys

import pymupdf

SIZE = 96
ICONS = {
    "badge": "id-card", "clock": "clock", "ebook": "book-open", "fonts": "type",
    "help": "circle-question-mark", "image": "image", "info": "info", "list": "list-checks",
    "net": "wifi", "news": "newspaper", "qr": "qr-code", "weather": "cloud-sun",
    "transfer": "upload", "settings": "settings", "code": "file-code", "sun": "sun", "cloud": "cloud",
    "rain": "cloud-rain", "snow": "cloud-snow", "storm": "cloud-lightning", "fog": "cloud-fog",
}


def rasterize(path: str) -> bytes:
    svg = open(path, encoding="utf-8").read()
    svg = svg.replace("currentColor", "#000000")
    svg = re.sub(r'stroke-width="[^"]*"', 'stroke-width="1.75"', svg)
    doc = pymupdf.open(stream=svg.encode(), filetype="svg")
    page = doc[0]
    zoom = SIZE / page.rect.width
    pix = page.get_pixmap(matrix=pymupdf.Matrix(zoom, zoom), colorspace=pymupdf.csGRAY, alpha=False)
    out = bytearray()
    for y in range(SIZE):
        for xb in range(0, SIZE, 8):
            b = 0
            for x in range(8):
                if pix.samples[y * pix.stride + xb + x] < 128:
                    b |= 0x80 >> x
            out.append(b)
    return bytes(out)


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    with open(dst, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n// tools/gen_icons.py が生成。手で直さないこと。\n")
        f.write("// 図柄は Lucide（https://lucide.dev、ISC ライセンス）。\n\n#include <stdint.h>\n\n")
        f.write(f"namespace pdos {{\nnamespace icons {{\n\ninline constexpr int16_t kSize = {SIZE};\n\n")
        for name, svg in ICONS.items():
            data = rasterize(f"{src}/{svg}.svg")
            f.write(f"inline constexpr uint8_t {name}[] = {{\n")
            for i in range(0, len(data), 24):
                f.write("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",\n")
            f.write("};\n\n")
        f.write("}  // namespace icons\n}  // namespace pdos\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
