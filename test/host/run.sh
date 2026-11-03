#!/bin/sh
# ハードに触らない部分のテスト（ボタンの解釈、天気とニュースの読み解き）。
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="${TMPDIR:-/tmp}/paperdiy-host-test"
c++ -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined -I "$ROOT/src" \
  "$ROOT/test/host/test_logic.cpp" "$ROOT/src/os/Buttons.cpp" "$ROOT/src/apps/Parse.cpp" -o "$OUT"
"$OUT"
