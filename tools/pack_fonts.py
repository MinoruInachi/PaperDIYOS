#!/usr/bin/env python3
"""フォントをフラッシュの font パーティション用にひとまとめにする。

複数の TTF（本文用・太字用）を 1 つのイメージにまとめ、先頭に索引を付ける。
ファームウェアはこれを esp_partition_mmap でそのまま読むので、PSRAM を
使わずに 5 MB 級の和文フォントが使える。

  python3 tools/pack_fonts.py assets/font.img \
      assets/reader-regular.ttf \
      --bold assets/reader-bold.ttf

書き込みは tools/flash_font.sh。
"""
import argparse
import os
import struct

MAGIC = b"JAFONT02"
HEADER = struct.Struct("<8sII")     # magic, count, reserved
ENTRY = struct.Struct("<IIB3x")     # offset, length, styleFlags, pad

STYLE_BOLD = 1 << 0
STYLE_ITALIC = 1 << 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="出力するイメージ")
    ap.add_argument("regular", help="本文用の TTF")
    ap.add_argument("--bold", help="太字用の TTF")
    ap.add_argument("--italic", help="斜体用の TTF")
    ap.add_argument("--partition-size", type=lambda s: int(s, 0), default=0x800000,
                    help="font パーティションの大きさ（既定 8MB）")
    args = ap.parse_args()

    faces = [(args.regular, 0)]
    if args.bold:
        faces.append((args.bold, STYLE_BOLD))
    if args.italic:
        faces.append((args.italic, STYLE_ITALIC))

    blobs = []
    for path, style in faces:
        with open(path, "rb") as f:
            blobs.append((f.read(), style, path))

    offset = HEADER.size + ENTRY.size * len(blobs)
    offset = (offset + 15) & ~15
    entries = []
    for data, style, _ in blobs:
        entries.append((offset, len(data), style))
        offset += len(data)
        offset = (offset + 15) & ~15

    if offset > args.partition_size:
        raise SystemExit(
            f"フォントが大きすぎます: {offset/1048576:.2f} MB > "
            f"パーティション {args.partition_size/1048576:.2f} MB\n"
            f"  tools/prepare_font.py に --kanji joyo を付けて絞ってください")

    with open(args.out, "wb") as out:
        out.write(HEADER.pack(MAGIC, len(blobs), 0))
        for off, length, style in entries:
            out.write(ENTRY.pack(off, length, style))
        for (off, length, _), (data, _, path) in zip(entries, blobs):
            out.seek(off)
            out.write(data)
            print(f"  {os.path.basename(path)}: {length/1048576:.2f} MB @ 0x{off:06X}")

    size = os.path.getsize(args.out)
    print(f"出力: {args.out}  {size/1048576:.2f} MB "
          f"（パーティション {args.partition_size/1048576:.0f} MB の "
          f"{size*100/args.partition_size:.0f}%）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
