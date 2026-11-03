#!/usr/bin/env python3
"""Shift_JIS（CP932）を Unicode に写す表を作る。

青空文庫のテキストは Shift_JIS で配られているので、読む前に UTF-8 へ直す。
その変換表をここで作って、ヘッダとしてファームウェアに埋め込む。

    python3 tools/gen_sjis_table.py src/SjisTable.h

表は「先頭バイト × 2 バイト目」の素直な格子。索引を引くだけで済むので速く、
探索も分岐も要らない。使わない升は 0 で埋まるが、フラッシュに置くので
PSRAM も DRAM も減らない。

半角（0x00-0x7F と 0xA1-0xDF）は計算で出せるので表には入れない。
"""

import sys

# 2 バイト文字の先頭バイト。CP932 はこの 2 つの帯を使う。
LEAD_RANGES = [(0x81, 0x9F), (0xE0, 0xFC)]
# 2 バイト目。0x7F だけは使われない。
TRAIL_LO, TRAIL_HI = 0x40, 0xFC


def lead_index(lead: int) -> int:
    """先頭バイトを 0 から始まる詰めた番号にする。"""
    offset = 0
    for lo, hi in LEAD_RANGES:
        if lo <= lead <= hi:
            return offset + (lead - lo)
        offset += hi - lo + 1
    return -1


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    dst = sys.argv[1]

    leads = sum(hi - lo + 1 for lo, hi in LEAD_RANGES)
    trails = TRAIL_HI - TRAIL_LO + 1
    table = [0] * (leads * trails)

    mapped = 0
    for lo, hi in LEAD_RANGES:
        for lead in range(lo, hi + 1):
            for trail in range(TRAIL_LO, TRAIL_HI + 1):
                if trail == 0x7F:
                    continue
                raw = bytes([lead, trail])
                try:
                    text = raw.decode("cp932")
                except UnicodeDecodeError:
                    continue
                if len(text) != 1:
                    continue
                code = ord(text)
                if code > 0xFFFF:
                    continue  # 基本多言語面の外は扱わない（青空文庫には出ない）
                table[lead_index(lead) * trails + (trail - TRAIL_LO)] = code
                mapped += 1

    lines = []
    for i in range(0, len(table), 12):
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in table[i:i + 12]) + ",")

    with open(dst, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        f.write("// tools/gen_sjis_table.py が作った。手で直さないこと。\n")
        f.write(f"//   python3 tools/gen_sjis_table.py {dst}\n")
        f.write("//\n")
        f.write("// Shift_JIS（CP932）の 2 バイト文字 → Unicode。\n")
        f.write("// 先頭バイトを詰めた番号にして、2 バイト目とで格子を引く。\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("namespace paperdiy {\n\n")
        f.write(f"inline constexpr uint8_t kSjisTrailLo = 0x{TRAIL_LO:02x};\n")
        f.write(f"inline constexpr uint8_t kSjisTrailHi = 0x{TRAIL_HI:02x};\n")
        f.write(f"inline constexpr uint16_t kSjisTrails = {trails};\n")
        f.write(f"inline constexpr uint16_t kSjisLeads = {leads};\n\n")
        f.write("inline constexpr uint16_t kSjisToUnicode[] = {\n")
        f.write("\n".join(lines))
        f.write("\n};\n\n")
        f.write("}  // namespace paperdiy\n")

    size = len(table) * 2
    print(f"{dst}: {mapped} 文字 / 表 {size} バイト（{size / 1024:.1f} KB）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
