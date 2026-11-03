#!/bin/sh
# JaBook のホストテストを全部走らせる。実機も PlatformIO も不要。
#
#   lib/JaBook/test/host/run.sh
#
# 日本語フォント（assets/reader-regular.ttf）があれば、続けて縦組みの
# 実レンダリングも行い、PGM 画像を書き出す。
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
BUILD="${TMPDIR:-/tmp}/jabook-tests"
mkdir -p "$BUILD"

echo "== ビルド =="
"$HERE/build.sh" "$BUILD" "$HERE/test_vertical.cpp"

echo
"$BUILD/test_vertical"

FONT="$ROOT/assets/reader-regular.ttf"
if [ -f "$FONT" ]; then
  echo
  echo "== 実フォントでの組版 =="
  "$HERE/build.sh" "$BUILD" "$ROOT/tools/preview.cpp"
  mkdir -p "$BUILD/pages" 2>/dev/null || true
  python3 "$ROOT/lib/JaBook/test/fixtures/make_ja_epub.py" "$BUILD/ja-sample.epub"
  "$BUILD/preview" "$BUILD/ja-sample.epub" "$FONT" "$BUILD/pages" --pages 2
  "$BUILD/preview" "$ROOT/lib/JaBook/test/fixtures/sample.txt" "$FONT" "$BUILD/pages" --pages 1
  echo "画像: $BUILD/pages"
else
  echo
  echo "（$FONT がないので実レンダリングは飛ばしました。"
  echo "  python3 tools/prepare_font.py assets/NotoSansJP.ttf assets/reader-regular.ttf）"
fi
