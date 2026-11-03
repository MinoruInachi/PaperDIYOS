# 縦書きと 16 階調の見本。A / B で切り替える。A 長押しで戻る。

import paperdiy as pd

LINES = ["吾輩は猫である。", "名前はまだ無い。", "どこで生れたか", "とんと見当がつかぬ。"]


def vertical():
    pd.clear()
    pd.set_pen(0)
    x = 450
    for line in LINES:
        pd.text_vertical(line, x, 60, 48)
        x -= 90
    pd.update(pd.UPDATE_TEXT)


def levels():
    pd.clear()
    for i in range(16):
        pd.set_pen(i)
        pd.rect(0, i * 60, 540, 60)
        pd.set_pen(15 if i < 8 else 0)
        pd.text(str(i), 20, i * 60 + 42, 32)
    pd.set_pen(0)
    pd.update(pd.UPDATE_QUALITY)


page = 0
while True:
    (vertical if page == 0 else levels)()
    pd.wait_key()
    page ^= 1
