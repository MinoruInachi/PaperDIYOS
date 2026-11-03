#!/bin/sh
# フォントイメージを font パーティション（0x410000）へ書き込む。
#
#   tools/flash_font.sh assets/font.img [シリアルポート]
#
# partitions.csv の font パーティションのオフセットと合わせてある。
set -e
IMG="${1:-assets/font.img}"
PORT="$2"
OFFSET=0x410000

if [ ! -f "$IMG" ]; then
  echo "イメージがありません: $IMG" >&2
  echo "  python3 tools/prepare_font.py assets/NotoSansJP.ttf assets/reader-regular.ttf" >&2
  echo "  python3 tools/pack_fonts.py assets/font.img assets/reader-regular.ttf" >&2
  exit 1
fi

ESPTOOL="$HOME/.platformio/penv/bin/esptool"
[ -x "$ESPTOOL" ] || ESPTOOL=$(command -v esptool)
[ -n "$ESPTOOL" ] || { echo "esptool が見つかりません" >&2; exit 1; }

set -- --chip esp32s3
[ -n "$PORT" ] && set -- "$@" --port "$PORT"
set -- "$@" --baud 921600 write-flash "$OFFSET" "$IMG"

echo "書き込み: $IMG -> $OFFSET"
exec "$ESPTOOL" "$@"
