# PaperDIY OS

[M5Stack PaperDIY](https://docs.m5stack.com/en/core/PaperDIY_Complete_Kit) 用の、
[Badger OS](https://learn.pimoroni.com/article/getting-started-with-badger-2040) 風の環境。
ランチャーから小さなアプリを選び、手が止まると眠り、ボタンを押すと続きから再開する。

- 表示は [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) の上に作ってある
- 電子書籍は**日本語の縦書き・横書き**に対応する
- **MicroPython** を内蔵。SD に `.py` を置けばアプリになる。USB から REPL も使える
- メニューは**日本語と英語**を設定で切り替えられる
- Wi-Fi と NTP も設定アプリから選べる（文字は 2 ボタンで打てる）

## できること

| アプリ | 内容 | 置き場所 |
|---|---|---|
| 電子書籍 | EPUB / テキスト / 青空文庫 ZIP。縦書き・横書き、ルビ、禁則、縦中横、しおり、目次、位置移動 | `/books` |
| 名札 | 会社名・名前・写真・項目 2 つ | `/badges/*.txt` |
| 時計 | NTP または手動で合わせる。毎分更新 | 設定アプリ（時刻） |
| 画像 | JPEG / PNG / BMP を 16 階調で。縦横切り替え | `/images` |
| 一覧 | チェックリスト | `/lists/checklist.txt` |
| QR | テキストを QR コードに | `/qrcodes/*.txt` |
| 天気 | Open-Meteo から今日〜明後日 | `/config/location.txt` |
| ニュース | RSS。記事のリンクは QR で出す | `/config/news.txt` |
| 無線 | Wi-Fi の接続情報 | 設定アプリ（Wi-Fi） |
| 転送 | ブラウザから `/books` へ本を出し入れ | — |
| 文字 / 情報 / 説明 | 文字と階調の見本、本体の情報、操作説明 | — |
| 設定 | 言語、Wi-Fi、時刻（NTP）、眠るまでの時間、電源 | 本体の NVS |
| Python のアプリ | `.py` を置くとランチャーに並ぶ。`hello`、`sysinfo`、`tategaki` を同梱 | `/examples` |

初めて SD カードを入れて起動すると、これらのフォルダと見本のファイルを作る。
すでにあるファイルは上書きしない。

## 操作

ボタンは左の側面の A（上）と B（下）、右下の電源ボタン。

| 操作 | 動き |
|---|---|
| A / B を押す | 前へ / 次へ（読書中はページをめくる） |
| B を長く押す | 決定（読書中はメニュー） |
| A を長く押す | 1 つ戻る（読書中は本棚、本棚の先頭ならランチャーへ） |
| A と B を同時に | どこからでもランチャーへ直行（Badger の A+C） |
| 電源ボタン 1 回 / 2 回 / 長押し | 電源オン・リセット / 電源オフ / 書き込みモード（PMIC の機能） |

電池のときは、2 分（設定で変更可）手が止まると眠る。A か B で起き、眠る前のアプリへ戻る。
押したボタンの操作もそのまま効くので、眠っていても B を押せば次のページが出る。
USB がつながっているあいだは眠らない。A と B を押したまま起こすとランチャーが開く。

「戻る」はどの画面でも A 長押しで、Python のアプリと REPL でも同じ。例外は文字入力だけで、打ちかけの
パスワードを誤操作で失わないよう、A 長押しは 1 字消す（空のときだけ戻る）。

## セットアップ

必要なもの: PaperDIY 本体、microSD（FAT32）、[PlatformIO](https://platformio.org/)、Python 3。

```sh
# 1. リポジトリを取ってきて、SDK のサブモジュールに差分を当てる
git clone https://github.com/MinoruInachi/PaperDIYOS.git
cd PaperDIYOS
tools/setup_sdk.sh

# 2. 日本語フォントを用意する
pip install fonttools brotli
curl -L -o assets/NotoSansJP.ttf \
  "https://raw.githubusercontent.com/google/fonts/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf"
python3 tools/prepare_font.py assets/NotoSansJP.ttf assets/reader-regular.ttf
python3 tools/pack_fonts.py assets/font.img assets/reader-regular.ttf

# 3. MicroPython のサブモジュールから組み込み用のソースを作る（要 make）
tools/setup_micropython.sh

# 4. 書き込む
pio run -e paperdiy -t upload
tools/flash_font.sh assets/font.img
```

フォントはフラッシュの `font` パーティション（8 MB）に置いて、そのままアドレス空間へ写す。
PSRAM は消費しない。フォントを焼いていないと、画面に英語で手順が出る。

## 設定

| 項目 | 内容 |
|---|---|
| 言語 | 日本語と英語を切り替える。本の中身には影響しない |
| Wi-Fi | 周りの電波から選んでパスワードを打つ。隠れた SSID も入力できる |
| 時刻（NTP） | いま合わせる、サーバー（一覧または入力）、地域、自動で合わせるか |
| 眠るまで | 30 秒〜10 分、または眠らない |

文字の入力は、A で右へ、B で下へ、B 長押しでキーを押し、A 長押しで 1 字消す。

Wi-Fi のパスワードは本体の NVS に**暗号化せず**置く。SD カードには書かないので、カードを抜いても
読まれないが、本体のフラッシュを読み出せる人には読める。NVS に接続先が無ければ、Badger の
`WIFI_CONFIG.py` のかわりに SD の `/config/wifi.txt`（`ssid = 名前`、`psk = パスワード`）も見る。

自動同期がオンなら、時計を開いたときと毎分の更新のときに、前回から 6 時間以上たっていれば合わせ直す。

### サブモジュール

| パス | 中身 | 使い方 |
|---|---|---|
| `freeink-sdk` | [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) | `patches/` を作業ツリーへ当てて使う。差分は親から見えないよう `ignore = dirty` にしてある |
| `micropython` | [MicroPython](https://github.com/micropython/micropython) v1.29.0 | `lib/micropython/embed` の生成元。ビルドには生成物だけを使う。履歴は浅く取る（`shallow = true`） |

- 取得は `tools/setup_sdk.sh` と `tools/setup_micropython.sh` に任せる。`git clone --recurse-submodules` は
  使わないこと。MicroPython の中のサブモジュール（各ポートの SDK など）まで取りに行ってしまう
- 版を上げるときは、サブモジュールを目的のコミットへ動かして親でコミットする。SDK は差分が当たったままだと
  動かせないので、先に `git -C freeink-sdk checkout .` と `git -C freeink-sdk clean -fd libs/hardware/BoardPaperDiy`
  で戻し、上げたあとで `tools/setup_sdk.sh` を実行する

### 工場出荷のファームへ戻す

[M5Burner の公式イメージ](https://burner.m5stack.com/share/firmware/UVVZ26)を書き込む。

## MicroPython

Badger OS と同じように、SD の `/examples` に `.py` を置くとランチャーに並ぶ。
`icon-名前.png` を同じ場所に置くとアイコンになる。電池で放っておくと眠り、起きたらスクリプトを先頭から
走らせ直す。A を長く押す（または A と B を同時に押す）と止まってランチャーへ戻る。エラーで止まるとトレースバックを画面に出す。

```python
import badger2040

display = badger2040.Badger2040()
display.set_pen(15)
display.clear()
display.set_pen(0)
display.text("こんにちは", 20, 100, scale=4)
display.update()

while True:
    if display.pressed(badger2040.BUTTON_DOWN):   # B を押した
        print("B")
    display.halt()
```

- MicroPython v1.29.0 を公式の embed ポートで組み込んでいる。ヒープは PSRAM に 2 MB
- `/lib` と、作業ディレクトリ（`/`）から import できる
- 組み込みモジュール: `sys`、`gc`、`math`、`time`、`os`、`json`、`re`、`binascii`、`random`、`struct`、
  `array`、`collections`、`io`、`deflate`、`micropython`

### ボタンの割り当て

Badger の 5 ボタンを、2 ボタンの押し方に割り当てている。

| PaperDIY | `badger2040` |
|---|---|
| A を押す | `BUTTON_UP` |
| B を押す | `BUTTON_DOWN` |
| B を長く押す | `BUTTON_B` |
| A を長く押す | スクリプトを止めて戻る（他のアプリと同じ） |
| A と B を同時に | スクリプトを止めてランチャーへ（Badger の A+C） |
| — | `BUTTON_C`（押せない） |

`BUTTON_A` を使うスクリプトは、先に `paperdiy.capture_back(True)` を呼ぶと A 長押しを受け取れる。
そのあいだスクリプトを止めるのは A+B だけになる。

### Badger との互換

`badger2040`、`badger_os`、`jpegdec`、`pngdec`、`qrcode`、`network`、`urequests`、`urllib`、`ntptime`、
`machine`、`version` を同じ名前で内蔵している。ただし画面は 540×960 なので、296×128 前提で座標を
決め打ちしたスクリプトは動いても左上に小さく出る。

Pimoroni の `badger_os/examples` を実機で走らせた結果:

| スクリプト | 結果 |
|---|---|
| `list`、`qrgen`、`fonts`、`image`、`help`、`info` | 動く（296×128 の配置のまま） |
| `badge` | 動く。`badge.txt` に書かれた写真が必要 |
| `clock` | 表示は動く。ボタンはピン割り込み（`BUTTONS[...].irq`）に頼っているので効かない |
| `ebook` | em ダッシュ（—）で止まる。スクリプトのバイト位置計算がこの字を数えていないため |
| `net_info` | 動く |
| `weather` | 動く。Badger 同梱のアイコン（`/icons/*.png`）が必要。地点はスクリプトの既定（英国シェフィールド） |
| `news` | 最初の記事と QR は出る。ボタンはピン（`machine.Pin(...).value()`）を直接読んでいるので効かない |

### paperdiy モジュール

本体を直接使う窓口。座標は 540×960、ペンは 0（黒）〜15（白）。

| 関数 | 内容 |
|---|---|
| `clear(pen=15)`、`set_pen(n)`、`pixel`、`rect`、`frame`、`line`、`circle`、`invert` | 描画 |
| `text(s, x, y, em=28)`、`text_wrap(s, x, y, w, em, line_height, max_lines)`、`text_vertical(s, x, y, em)`、`text_width(s, em)` | 文字（y はベースライン） |
| `image(path, x, y, w, h)`、`image_size(path)`、`bitmap(data, w, h, x, y, scale)`、`qr(text, x, y, size)` | 画像と QR |
| `update(UPDATE_QUALITY / TEXT / FAST / FASTEST)` | パネルへ送る（Fast は白黒だけ） |
| `key()`、`wait_key(timeout_ms)`、`wake_key()`、`capture_back(on)` | ボタン（`KEY_A`、`KEY_B`、`KEY_LONG_B`。`KEY_LONG_A` は `capture_back(True)` のときだけ） |
| `wifi_connect(sec)`、`wifi_stop()`、`wifi_online()`、`wifi_ip()`、`ntp_sync()`、`http_get(url)` | 無線 |
| `battery()`、`language()`、`deep_sleep(sec)`、`light_sleep(sec)`、`power_off()`、`reset()`、`launch(target)` | 本体 |

### USB から使う

mpremote や Thonny からは MicroPython の REPL として見える。つなぐと（Ctrl-C か Ctrl-A が届くと）
今のアプリを閉じて REPL に入り、A を長く押すか USB を閉じると元のアプリへ戻る。

```sh
pip install mpremote pyserial pillow
mpremote cp hello.py :/examples/hello.py   # アプリを入れる
mpremote repl                              # REPL
tools/pdiy.py repl                         # mpremote が無くても REPL に入れる
```

`tools/pdiy.py` は Python を使わずに本体を操作する道具。ボタンを押さずに操作でき、画面の中身も取り出せる。

```sh
tools/pdiy.py info                         # 状態を見る
tools/pdiy.py key b                        # B を押す（a b / 大文字は長押し / ab は同時）
tools/pdiy.py shot screen.png              # いまの画面を PNG に
tools/pdiy.py app ebook                    # アプリを切り替える（py:/examples/hello.py も可）
tools/pdiy.py push 本.epub /books/本.epub  # SD へ送る
tools/pdiy.py pull /books/本.epub 本.epub  # SD から取り出す
tools/pdiy.py ls /books                    # SD の中を見る
```

## C++ でアプリを足す

Python で足りないとき（速度が要る、SDK を直接使いたいなど）は C++ で書く。`src/apps/` にファイルを足し、
`Apps.h` に宣言を、`Apps.cpp` の一覧に 1 行を足すとランチャーに並ぶ。

```cpp
class HelloApp : public App {
 public:
  const char* id() const override { return "hello"; }             // 状態の保存に使う名前
  const char* name() const override { return TR("やあ", "Hello"); }  // ランチャーに出す名前
  const uint8_t* icon() const override { return icons::info; }

  void start(bool resumed, Key wake) override { render(); }  // 起動・復帰のとき
  void key(Key k) override {                                 // ボタン（同時押しは OS が受ける）
    if (k == Key::B) { ++count_; render(); }
    if (k == Key::LongA) os.goHome();
  }
  void suspend() override { store::save("hello", count_); }  // 眠る前に保存

 private:
  void render() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(name());
    g.setPen(0);                                  // ペンは Badger と同じ 0（黒）〜15（白）
    g.textCentered(TR("こんにちは", "Hello"), Gfx::W / 2, 400, 48);
    ui::footer(TR("B:数える A長押し:戻る", "B:Count Hold A:Back"));
    g.update(Update::Fast);                       // Quality / Text / Fast / Fastest
  }
  int count_ = 0;
};
```

表示の更新にかかる時間の実測値は、`Fast` が約 0.4 秒、`Text` と `Quality` が約 1.4 秒。

## ハードウェアの要点

回路図（K159-B）、M5GFX、M5Unified から確認した内容。

- ESP32-S3、16 MB フラッシュ、8 MB octal PSRAM、ED047TC1（960×540、16 階調）のパラレル接続
- パネルの電源は M5PM1 の GPIO2（`EPD_PWR`）と G46（`BST_EN`）
- KEY1=G4、KEY2=G3。deep sleep からの起床に使える
- **RTC チップが無い**。時刻は ESP32-S3 内蔵の RTC タイマーで数えるので、deep sleep とリセットは
  越えるが電源を切ると失う。内蔵発振器は精度も低いので NTP で合わせる
- タッチ、IMU、ブザー、フロントライトは無い

眠りは回路図の電源モード図に沿う。ふつうは L2 DeepSleep で、起きると起動からやり直すので
状態は NVS と SD に置く。時計のように定期的に描き直すアプリは light sleep で待つ（起きるたびの
全画面点滅を避けるため。電流はそのぶん多い）。

## SDK への差分（patches/）

FreeInk SDK に PaperDIY の定義が無いので、次の差分を当てて使う。

| パッチ | 内容 |
|---|---|
| `0001-paperdiy-board-and-gray-canvas` | `FREEINK_DEVICE_PAPERDIY`、`BoardPaperDiy` ライブラリ、16 階調キャンバスの API |
| `0002-tjpgd-disable-fastdecode` | 同梱の TJpgDec の `JD_FASTDECODE=1` は AC 係数を壊し、挿絵がまだらになるので 0 にする |
| `0003-yokogaki-skip-ruby-annotations` | 横書きでルビの注記が本文に混ざらないようにする |

`platformio.ini` の `-fno-merge-constants` も必須。FreeInkBook と M5GFX は別々に miniz を持っていて、
miniz の `"\03\03\013"[dist - 16]` が「文字列 - 16」という再配置になるため、リンカが文字列を統合すると
指す先がずれて ZIP の展開が壊れる（すべての EPUB が `IoError` で開けなくなる）。

## テスト

```sh
test/host/run.sh               # ボタンの解釈、天気とニュースの読み解き
lib/JaBook/test/host/run.sh    # 縦組みエンジン（要 tools/setup_sdk.sh）
python3 tools/check_glyphs.py  # 画面の文字列がフォントに入っているか（要 fonttools）
```

## ファイル構成

```
src/os/        OS 本体（Os、ボタン、描画、言語、文字入力、状態保存、無線、USB コンソール）
src/apps/      アプリ（Launcher、EbookApp、ClockApp、SettingsApp、SimpleApps、NetApps）
src/python/    Python アプリの実行と C++ への橋（PythonApp、PyBridge）
python/lib/    本体に内蔵する Python モジュール（badger2040 など）
python/examples/  最初に SD へ置くサンプル
lib/micropython/port/  MicroPython のポート層（設定、os と open、paperdiy モジュール）
lib/micropython/embed/ tools/setup_micropython.sh が生成（git 対象外）
freeink-sdk/    FreeInk SDK（サブモジュール）
micropython/    MicroPython（サブモジュール）
src/reader/    読書エンジン（本を開く・組む・描く、本棚、しおり、青空文庫変換）
lib/JaBook/    縦組みエンジン（ハード非依存）
tools/         SDK の準備、フォント、アイコン生成、pdiy.py
patches/       SDK への差分
test/host/     ホストテスト
```

## いまできていないこと

- Python の `socket` と `machine.Pin`。通信は `urequests.get`（GET のみ）か `paperdiy.http_get` を使う
- Python の `display.text` の回転（`angle`）。縦書きは `paperdiy.text_vertical` を使う
- `.mpy` の読み込みとネイティブコード（`@micropython.native`）
- 縦書きでの挿絵の配置、脚注リンク、圏点（縦組みエンジンの制限）
- プログレッシブ JPEG の画像アプリでの表示（Badger と同じ制限）
- 転送の Web 画面に合言葉は無い。家の中で、使うときだけ上げる前提
- `/books` 以外への無線での転送（`tools/pdiy.py push` か SD カードリーダーを使う）

## クレジット

- FreeInk SDK（MIT）、M5GFX（MIT）、MicroPython（MIT）
- アプリの構成と操作は [Pimoroni Badger 2040](https://github.com/pimoroni/badger2040)（MIT）を参考にした
- アイコンは [Lucide](https://lucide.dev)（ISC）
