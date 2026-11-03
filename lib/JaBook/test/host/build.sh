#!/bin/sh
# JaBook のホストビルド。実機も PlatformIO も要らない — FreeInkBook と
# JaBook はどちらも freestanding C++17 なので、そのまま Mac / Linux で
# 組版してレンダリングまで確認できる。
#
#   使い方: build.sh <出力ディレクトリ> <ビルドする .cpp> [追加フラグ...]
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
SDK="$ROOT/freeink-sdk/libs/book/FreeInkBook"
FONT="$ROOT/freeink-sdk/libs/font/FreeInkFont"
OUT="$1"
MAIN="$2"
shift 2

INCLUDES="-I$SDK/include -I$SDK/third_party/expat -I$SDK/third_party/miniz \
-I$SDK/third_party/libunibreak -I$SDK/third_party/pngle -I$SDK/third_party/tjpgd \
-I$SDK/third_party/stb -I$FONT/include -I$FONT/third_party/stb -I$ROOT/lib/JaBook/include"

mkdir -p "$OUT/obj"

cc -O1 -std=c99 $INCLUDES -c "$FONT/src/FontAlloc.c" -o "$OUT/obj/FontAlloc.o"

# ベンダ C 実装（初回だけコンパイルして使い回す）
for src in miniz_impl expat_xmlparse expat_xmlrole expat_xmltok unibreak_impl pngle_impl tjpgd_impl; do
  if [ ! -f "$OUT/obj/$src.o" ]; then
    cc -O1 -std=c99 $INCLUDES -c "$SDK/src/vendor/$src.c" -o "$OUT/obj/$src.o"
  fi
done

CORE="$SDK/src/FreeInkBook.cpp $SDK/src/BookCatalog.cpp $SDK/src/epub/ZipCatalog.cpp \
$SDK/src/epub/XmlSax.cpp $SDK/src/epub/PackageParsers.cpp $SDK/src/epub/ImageProbe.cpp \
$SDK/src/text/EntityFilter.cpp $SDK/src/text/Hyphenator.cpp $SDK/src/css/Css.cpp \
$SDK/src/layout/ChapterLayout.cpp $SDK/src/cache/PageCache.cpp \
$SDK/src/render/ImageRenderer.cpp $SDK/src/render/PageRenderer.cpp \
$FONT/src/TtfFont.cpp $FONT/src/Gsub.cpp $FONT/src/Gpos.cpp"

JABOOK="$ROOT/lib/JaBook/src/JaTextRules.cpp $ROOT/lib/JaBook/src/JaVerticalLayout.cpp \
$ROOT/lib/JaBook/src/JaVerticalRenderer.cpp"

exec c++ -std=c++17 -O1 -Wall -Wextra -Werror $INCLUDES "$@" \
  $CORE $JABOOK "$MAIN" "$OUT"/obj/*.o -o "$OUT/$(basename "$MAIN" .cpp)"
