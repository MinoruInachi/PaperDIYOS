#!/bin/sh
# MicroPython のサブモジュールを取ってきて、組み込み用のソースを lib/micropython/embed に作る。
# 版は親リポジトリが記録しているコミットで決まる。
# C のモジュール（mod_*.c）に新しい名前を足したら、もう一度実行すること。
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/micropython"

# MicroPython 自身のサブモジュール（各ポートの SDK など）は要らないので再帰しない。
# 履歴も要らないので浅く取る（.gitmodules の shallow = true）。
git -C "$ROOT" submodule update --init --depth 1 micropython

cd "$ROOT/lib/micropython/port"
rm -rf build-embed ../embed
make -s extras MICROPYTHON_TOP="$SRC" PACKAGE_DIR=../embed
rm -rf build-embed
echo "生成しました: lib/micropython/embed ($(git -C "$SRC" rev-parse --short HEAD))"
