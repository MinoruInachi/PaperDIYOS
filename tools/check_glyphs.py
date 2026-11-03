#!/usr/bin/env python3
"""画面に出す文字列がフォントに入っているかを調べる。

字形が無い字は描かれず、空白になるだけで気づきにくい（☑ と ℃ で実際に起きた）。

  pip install fonttools
  python3 tools/check_glyphs.py [assets/reader-regular.ttf]
"""
import glob
import re
import sys

from fontTools.ttLib import TTFont

FONT = sys.argv[1] if len(sys.argv) > 1 else "assets/reader-regular.ttf"
SOURCES = ["src/apps/*.cpp", "src/os/*.cpp", "src/python/PythonApp.cpp", "python/lib/*.py", "python/examples/*.py"]
STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')

cmap = TTFont(FONT).getBestCmap()
missing = 0
for pattern in SOURCES:
    for path in sorted(glob.glob(pattern)):
        for number, line in enumerate(open(path, encoding="utf-8"), 1):
            code = line.split("//")[0] if path.endswith((".cpp", ".h")) else line.split("#")[0]
            for match in STRING.finditer(code):
                for ch in match.group(1):
                    if ord(ch) >= 0x80 and ord(ch) not in cmap:
                        print(f"{path}:{number}: U+{ord(ch):04X} {ch}")
                        missing += 1
print("字形の無い字はありません" if missing == 0 else f"{missing} 字が欠けています")
sys.exit(1 if missing else 0)
