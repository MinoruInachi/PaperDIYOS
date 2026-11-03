#!/usr/bin/env python3
"""テスト用の日本語 EPUB を作る（ルビ・見出し・縦中横・禁則の見本入り）。

  python3 make_ja_epub.py 出力.epub
"""
import os
import struct
import sys
import zipfile
import zlib


def make_png(width, height):
    """テスト用のグレースケール PNG（放射状のグラデーションと枠）。"""
    cx, cy = width / 2, height / 2
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            v = int(max(0, min(255, 255 - d * 255 / (min(cx, cy) * 1.4))))
            if x < 3 or y < 3 or x >= width - 3 or y >= height - 3:
                v = 0  # 枠線
            row.append(v)
        rows.append(bytes([0]) + bytes(row))
    raw = b"".join(rows)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))

COVER = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="ja">
<head><title>表紙</title><link rel="stylesheet" type="text/css" href="../style.css"/></head>
<body>
<div><img src="../images/cover.png" alt="表紙"/></div>
</body></html>
"""

CH1 = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="ja">
<head><title>第一章</title><link rel="stylesheet" type="text/css" href="../style.css"/></head>
<body>
<h1>第一章　組版の見本</h1>
<p id="p1">　<ruby>吾輩<rp>（</rp><rt>わがはい</rt><rp>）</rp></ruby>は猫である。名前はまだ無い。どこで生れたか<ruby>頓<rt>とん</rt></ruby>と見当がつかぬ。</p>
<p>　「おい、そこの君！」と呼ばれて振り返ると、そこには（見たこともない）長い長い廊下が続いていた……。</p>
<p>　彼は12歳のとき、<b>E-ink</b>の端末で1000ページを超える本を読んだ。それは1冊で3年ぶんの読書量に相当したという。</p>
<p>　行頭に句読点が来ないこと、行末に始め括弧が残らないこと——この二つが<i>禁則処理</i>の基本である。小書きの「っ」「ゃ」、長音符「ー」、波ダッシュ「〜」の向きも見どころ。</p>
<h2>節　入れ子の見出し</h2>
<p>　次の段落の前に挿絵が入る。縦組みでは 1 枚を 1 ページとして版面いっぱいに置く。</p>
<div><img src="../images/plate.png" alt="挿絵"/></div>
<p>　<ruby>薔薇<rt>ばら</rt></ruby>の<ruby>花束<rt>はなたば</rt></ruby>を抱えた<ruby>少女<rt>しょうじょ</rt></ruby>が、<ruby>石畳<rt>いしだたみ</rt></ruby>の道を歩いていく。ルビが連続する段落で行送りが崩れないかを見る。</p>
</body></html>
"""

CH2 = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="ja">
<head><title>第二章</title><link rel="stylesheet" type="text/css" href="../style.css"/></head>
<body>
<h1>第二章　長文</h1>
""" + "\n".join(
    f"<p>　{i+1}番目の段落である。ページ送りとキャッシュの検証のために、"
    "同じような文章を繰り返し並べている。約物や欧文 (ASCII) が混ざったときの"
    "字送りも確認したい。</p>" for i in range(40)
) + """
</body></html>
"""

# 実際の日本語縦組み EPUB と同じく、CSS で縦組みを宣言する。
# リーダはこれを見て縦組み／横組みを決める。
CSS = """@charset "UTF-8";
html {
  -epub-writing-mode: vertical-rl;
  writing-mode: vertical-rl;
}
body { line-height: 1.8; }
h1, h2 { font-weight: bold; }
"""

OPF = """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>縦組みテスト</dc:title>
    <dc:creator>試験用</dc:creator>
    <dc:language>ja</dc:language>
    <dc:identifier id="bookid">urn:uuid:jabook-test-0001</dc:identifier>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="css" href="style.css" media-type="text/css"/>
    <item id="cover" href="text/cover.xhtml" media-type="application/xhtml+xml"/>
    <item id="coverimg" href="images/cover.png" media-type="image/png" properties="cover-image"/>
    <item id="plate" href="images/plate.png" media-type="image/png"/>
    <item id="ch1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine page-progression-direction="rtl">
    <itemref idref="cover"/>
    <itemref idref="ch1"/>
    <itemref idref="ch2"/>
  </spine>
</package>
"""

NAV = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="ja">
<head><title>目次</title></head>
<body>
<nav epub:type="toc"><ol>
  <li><a href="text/ch1.xhtml">第一章　組版の見本</a></li>
  <li><a href="text/ch2.xhtml">第二章　長文</a></li>
</ol></nav>
</body></html>
"""

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "ja-sample.epub"
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w") as z:
        # mimetype は無圧縮で先頭に置く（OCF の決まり）
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip",
                   compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", OPF, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/nav.xhtml", NAV, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/style.css", CSS, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/text/cover.xhtml", COVER, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/images/cover.png", make_png(600, 900), zipfile.ZIP_STORED)
        z.writestr("OEBPS/images/plate.png", make_png(480, 320), zipfile.ZIP_STORED)
        z.writestr("OEBPS/text/ch1.xhtml", CH1, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/text/ch2.xhtml", CH2, zipfile.ZIP_DEFLATED)
    print(f"作成: {out} ({os.path.getsize(out)} バイト)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
