#!/bin/sh
# FreeInk SDK のサブモジュールを取ってきて、本プロジェクト用の差分を当てる。
# 版は親リポジトリが記録しているコミットで決まる。
# 何度実行しても安全（当たっていれば黙って飛ばす）。
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK="$ROOT/freeink-sdk"

# SDK 自身のサブモジュール（Lucide のアイコン）は要らないので再帰しない。
git -C "$ROOT" submodule update --init freeink-sdk

for p in "$ROOT"/patches/*.patch; do
  [ -f "$p" ] || continue
  if git -C "$SDK" apply --check "$p" 2>/dev/null; then
    git -C "$SDK" apply "$p"
    echo "適用: $(basename "$p")"
  elif git -C "$SDK" apply --reverse --check "$p" 2>/dev/null; then
    echo "適用済み: $(basename "$p")"
  else
    echo "当てられません（SDK の版が違う可能性）: $(basename "$p")" >&2
    exit 1
  fi
done
