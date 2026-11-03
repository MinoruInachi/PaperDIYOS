# PaperDIY OS

A [Badger OS](https://learn.pimoroni.com/article/getting-started-with-badger-2040)-style environment for the
[M5Stack PaperDIY](https://docs.m5stack.com/en/core/PaperDIY_Complete_Kit).
Pick a small app from the launcher. The device sleeps when left alone and resumes where you left off at the press of a button.

- The display is built on the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk)
- The e-book reader supports **Japanese in vertical and horizontal writing**
- **MicroPython** is built in. Put a `.py` file on the SD card and it becomes an app. A REPL is available over USB
- Menus switch between **Japanese and English** in Settings
- Wi-Fi and NTP can be set up from the Settings app (text entry works with two buttons)

## Features

| App | Description | Location |
|---|---|---|
| E-book | EPUB, plain text, and Aozora Bunko ZIP. Vertical and horizontal writing, ruby, kinsoku (Japanese line-breaking rules), tate-chu-yoko (horizontal digits in vertical text), bookmarks, table of contents, go to position | `/books` |
| Badge | Company name, name, photo, and two detail fields | `/badges/*.txt` |
| Clock | Set by NTP or by hand. Updates every minute | Settings app (Clock) |
| Images | JPEG, PNG, and BMP in 16 gray levels. Portrait or landscape | `/images` |
| List | Checklist | `/lists/checklist.txt` |
| QR | Turns text into a QR code | `/qrcodes/*.txt` |
| Weather | Today through the day after tomorrow, from Open-Meteo | `/config/location.txt` |
| News | RSS. Article links are shown as QR codes | `/config/news.txt` |
| Network | Wi-Fi connection details | Settings app (Wi-Fi) |
| Transfer | Manage books in `/books` from a browser | — |
| Fonts / Info / Help | Text and gray-level samples, device information, usage guide | — |
| Settings | Language, Wi-Fi, clock (NTP), sleep timeout, power | Device NVS |
| Python apps | Put a `.py` file in place and it appears in the launcher. `hello`, `sysinfo`, and `tategaki` are included | `/examples` |

The first boot with an SD card creates these folders and some sample files.
Existing files are not overwritten.

## Controls

Buttons A (upper) and B (lower) are on the left side. The power button is at the bottom right.

| Action | Result |
|---|---|
| Press A / B | Previous / next (turns the page while reading) |
| Hold B | OK (opens the menu while reading) |
| Hold A | Back one step (to the library while reading, to the launcher from the top of the library) |
| A and B together | Straight to the launcher from anywhere (Badger's A+C) |
| Power button once / twice / hold | Power on or reset / power off / download mode (handled by the PMIC) |

On battery, the device sleeps after 2 minutes without input (configurable in Settings). A or B wakes it and returns to the app you were using.
The press also counts as an input, so pressing B while asleep shows the next page.
The device does not sleep while USB is connected. Waking it with A and B held opens the launcher.

"Back" is Hold A on every screen, including Python apps and the REPL. The only exception is text entry, where
Hold A deletes one character (and goes back only when the field is empty), so a mistaken press cannot lose a partly typed password.

## Setup

You need a PaperDIY, a microSD card (FAT32), [PlatformIO](https://platformio.org/), and Python 3.

```sh
# 1. Get the repository and apply the patches to the SDK submodule
git clone https://github.com/MinoruInachi/PaperDIYOS.git
cd PaperDIYOS
tools/setup_sdk.sh

# 2. Prepare the Japanese font
pip install fonttools brotli
curl -L -o assets/NotoSansJP.ttf \
  "https://raw.githubusercontent.com/google/fonts/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf"
python3 tools/prepare_font.py assets/NotoSansJP.ttf assets/reader-regular.ttf
python3 tools/pack_fonts.py assets/font.img assets/reader-regular.ttf

# 3. Generate the embedded sources from the MicroPython submodule (requires make)
tools/setup_micropython.sh

# 4. Flash
pio run -e paperdiy -t upload
tools/flash_font.sh assets/font.img
```

The font lives in the `font` flash partition (8 MB) and is mapped directly into the address space,
so it uses no PSRAM. If the font has not been flashed, the screen shows instructions in English.

## Settings

| Item | Description |
|---|---|
| Language | Switches between Japanese and English. Does not affect book contents |
| Wi-Fi | Choose from nearby networks and type the password. Hidden SSIDs can be entered too |
| Clock (NTP) | Sync now, server (from a list or typed in), time zone, auto sync |
| Sleep after | 30 seconds to 10 minutes, or never |

For text entry, A moves right, B moves down, Hold B presses the key, and Hold A deletes one character.

The Wi-Fi password is stored in the device's NVS **without encryption**. It is not written to the SD card, so removing the card
does not expose it, but anyone who can read the device's flash can read it. If NVS has no network set, the device also
looks at `/config/wifi.txt` on the SD card (`ssid = name`, `psk = password`), which takes the place of Badger's `WIFI_CONFIG.py`.

With auto sync on, the clock is synced again when the Clock app opens and at each per-minute update, if 6 hours or more have passed since the last sync.

### Submodules

| Path | Contents | Usage |
|---|---|---|
| `freeink-sdk` | [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) | `patches/` is applied to its working tree. `ignore = dirty` keeps those changes out of the parent's status |
| `micropython` | [MicroPython](https://github.com/micropython/micropython) v1.29.0 | Source for `lib/micropython/embed`. The build uses only the generated files. Fetched with shallow history (`shallow = true`) |

- Leave fetching to `tools/setup_sdk.sh` and `tools/setup_micropython.sh`. Do not use `git clone --recurse-submodules`:
  it also fetches the submodules inside MicroPython (the SDKs for each port, among others)
- To update a version, move the submodule to the target commit and commit in the parent. The SDK cannot move while the patches are applied,
  so first revert them with `git -C freeink-sdk checkout .` and `git -C freeink-sdk clean -fd libs/hardware/BoardPaperDiy`,
  then run `tools/setup_sdk.sh` after updating

### Restoring the factory firmware

Flash the [official M5Burner image](https://burner.m5stack.com/share/firmware/UVVZ26).

## MicroPython

As in Badger OS, a `.py` file in `/examples` on the SD card appears in the launcher.
An `icon-name.png` in the same place becomes its icon. Left alone on battery, the device sleeps, and on waking it runs the script again
from the top. Holding A (or pressing A and B together) stops the script and returns to the launcher. If a script stops with an error, the traceback is shown on the screen.

```python
import badger2040

display = badger2040.Badger2040()
display.set_pen(15)
display.clear()
display.set_pen(0)
display.text("こんにちは", 20, 100, scale=4)
display.update()

while True:
    if display.pressed(badger2040.BUTTON_DOWN):   # B was pressed
        print("B")
    display.halt()
```

- MicroPython v1.29.0 is embedded through the official embed port, with a 2 MB heap in PSRAM
- Modules can be imported from `/lib` and the working directory (`/`)
- Built-in modules: `sys`, `gc`, `math`, `time`, `os`, `json`, `re`, `binascii`, `random`, `struct`,
  `array`, `collections`, `io`, `deflate`, `micropython`

### Button mapping

Badger's five buttons are mapped onto the ways of pressing two buttons.

| PaperDIY | `badger2040` |
|---|---|
| Press A | `BUTTON_UP` |
| Press B | `BUTTON_DOWN` |
| Hold B | `BUTTON_B` |
| Hold A | Stops the script and goes back (same as other apps) |
| A and B together | Stops the script and goes to the launcher (Badger's A+C) |
| — | `BUTTON_C` (cannot be pressed) |

A script that uses `BUTTON_A` can receive Hold A by calling `paperdiy.capture_back(True)` first.
While that is in effect, only A+B stops the script.

### Badger compatibility

`badger2040`, `badger_os`, `jpegdec`, `pngdec`, `qrcode`, `network`, `urequests`, `urllib`, `ntptime`,
`machine`, and `version` are built in under the same names. The screen is 540×960, however, so scripts with coordinates
hard-coded for 296×128 may run but appear small in the top-left corner.

Results of running Pimoroni's `badger_os/examples` on the device:

| Script | Result |
|---|---|
| `list`, `qrgen`, `fonts`, `image`, `help`, `info` | Work (with their 296×128 layout) |
| `badge` | Works. Needs the photo named in `badge.txt` |
| `clock` | The display works. The buttons do not, because they rely on pin interrupts (`BUTTONS[...].irq`) |
| `ebook` | Stops at an em dash (—), because the script's byte-position arithmetic does not account for this character |
| `net_info` | Works |
| `weather` | Works. Needs Badger's bundled icons (`/icons/*.png`). The location is the script's default (Sheffield, UK) |
| `news` | Shows the first article and its QR code. The buttons do not work, because they read pins directly (`machine.Pin(...).value()`) |

### The paperdiy module

The direct interface to the device. Coordinates are 540×960, and pens run from 0 (black) to 15 (white).

| Functions | Purpose |
|---|---|
| `clear(pen=15)`, `set_pen(n)`, `pixel`, `rect`, `frame`, `line`, `circle`, `invert` | Drawing |
| `text(s, x, y, em=28)`, `text_wrap(s, x, y, w, em, line_height, max_lines)`, `text_vertical(s, x, y, em)`, `text_width(s, em)` | Text (y is the baseline) |
| `image(path, x, y, w, h)`, `image_size(path)`, `bitmap(data, w, h, x, y, scale)`, `qr(text, x, y, size)` | Images and QR codes |
| `update(UPDATE_QUALITY / TEXT / FAST / FASTEST)` | Sends to the panel (Fast is black and white only) |
| `key()`, `wait_key(timeout_ms)`, `wake_key()`, `capture_back(on)` | Buttons (`KEY_A`, `KEY_B`, `KEY_LONG_B`. `KEY_LONG_A` only with `capture_back(True)`) |
| `wifi_connect(sec)`, `wifi_stop()`, `wifi_online()`, `wifi_ip()`, `ntp_sync()`, `http_get(url)` | Wireless |
| `battery()`, `language()`, `deep_sleep(sec)`, `light_sleep(sec)`, `power_off()`, `reset()`, `launch(target)` | Device |

### Using it over USB

mpremote and Thonny see the device as a MicroPython REPL. Connecting (when a Ctrl-C or Ctrl-A arrives)
closes the current app and enters the REPL. Holding A or closing the USB port returns to the app.

```sh
pip install mpremote pyserial pillow
mpremote cp hello.py :/examples/hello.py   # Install an app
mpremote repl                              # REPL
tools/pdiy.py repl                         # REPL without mpremote
```

`tools/pdiy.py` is a tool for operating the device without writing Python. It can operate the device without pressing buttons and can read back the screen contents.

```sh
tools/pdiy.py info                         # Show status
tools/pdiy.py key b                        # Press B (a, b; uppercase holds; ab presses both)
tools/pdiy.py shot screen.png              # Save the current screen as a PNG
tools/pdiy.py app ebook                    # Switch apps (py:/examples/hello.py also works)
tools/pdiy.py push book.epub /books/book.epub  # Send a file to the SD card
tools/pdiy.py pull /books/book.epub book.epub  # Get a file from the SD card
tools/pdiy.py ls /books                    # List the SD card
```

## Adding apps in C++

When Python is not enough (you need speed, or want to use the SDK directly), write the app in C++. Add a file to `src/apps/`,
a declaration to `Apps.h`, and one line to the list in `Apps.cpp`, and it appears in the launcher.

```cpp
class HelloApp : public App {
 public:
  const char* id() const override { return "hello"; }             // Name used for saved state
  const char* name() const override { return TR("やあ", "Hello"); }  // Name shown in the launcher
  const uint8_t* icon() const override { return icons::info; }

  void start(bool resumed, Key wake) override { render(); }  // On launch and resume
  void key(Key k) override {                                 // Buttons (the OS handles A+B)
    if (k == Key::B) { ++count_; render(); }
    if (k == Key::LongA) os.goHome();
  }
  void suspend() override { store::save("hello", count_); }  // Save before sleeping

 private:
  void render() {
    Gfx& g = os.gfx();
    g.clear();
    ui::header(name());
    g.setPen(0);                                  // Pens run from 0 (black) to 15 (white), as on Badger
    g.textCentered(TR("こんにちは", "Hello"), Gfx::W / 2, 400, 48);
    ui::footer(TR("B:数える A長押し:戻る", "B:Count Hold A:Back"));
    g.update(Update::Fast);                       // Quality / Text / Fast / Fastest
  }
  int count_ = 0;
};
```

Measured display update times are about 0.4 seconds for `Fast` and about 1.4 seconds for `Text` and `Quality`.

## Hardware notes

Confirmed from the schematic (K159-B), M5GFX, and M5Unified.

- ESP32-S3, 16 MB flash, 8 MB octal PSRAM, ED047TC1 (960×540, 16 gray levels) on a parallel bus
- Panel power is controlled by M5PM1 GPIO2 (`EPD_PWR`) and G46 (`BST_EN`)
- KEY1=G4, KEY2=G3. Both can wake the device from deep sleep
- **There is no RTC chip.** Time is kept by the ESP32-S3's internal RTC timer, so it survives deep sleep and reset
  but is lost at power off. The internal oscillator is also imprecise, so the clock is set by NTP
- There is no touch panel, IMU, buzzer, or frontlight

Sleep follows the power mode diagram in the schematic. Normally the device uses L2 DeepSleep, which restarts from boot on waking,
so state is kept in NVS and on the SD card. Apps that redraw periodically, such as the clock, wait in light sleep (to avoid a
full-screen flash at each wake, at the cost of higher current).

## SDK patches (patches/)

The FreeInk SDK has no definition for the PaperDIY, so the following patches are applied.

| Patch | Description |
|---|---|
| `0001-paperdiy-board-and-gray-canvas` | `FREEINK_DEVICE_PAPERDIY`, the `BoardPaperDiy` library, and an API for the 16-level canvas |
| `0002-tjpgd-disable-fastdecode` | Sets `JD_FASTDECODE` to 0, because `JD_FASTDECODE=1` in the bundled TJpgDec corrupts AC coefficients and leaves illustrations mottled |
| `0003-yokogaki-skip-ruby-annotations` | Keeps ruby annotations out of the body text in horizontal writing |

`-fno-merge-constants` in `platformio.ini` is also required. FreeInkBook and M5GFX each carry their own miniz,
and miniz's `"\03\03\013"[dist - 16]` becomes a "string minus 16" relocation. When the linker merges the strings,
the reference points to the wrong place and ZIP extraction breaks (every EPUB then fails to open with `IoError`).

## Tests

```sh
test/host/run.sh               # Button decoding, weather and news parsing
lib/JaBook/test/host/run.sh    # Vertical layout engine (requires tools/setup_sdk.sh)
python3 tools/check_glyphs.py  # Whether on-screen strings are in the font (requires fonttools)
```

## File layout

```
src/os/        OS core (Os, buttons, drawing, language, text entry, saved state, wireless, USB console)
src/apps/      Apps (Launcher, EbookApp, ClockApp, SettingsApp, SimpleApps, NetApps)
src/python/    Running Python apps and the bridge to C++ (PythonApp, PyBridge)
python/lib/    Python modules built into the firmware (badger2040 and others)
python/examples/  Samples placed on the SD card at first boot
lib/micropython/port/  MicroPython port layer (configuration, os and open, the paperdiy module)
lib/micropython/embed/ Generated by tools/setup_micropython.sh (not tracked by git)
freeink-sdk/    FreeInk SDK (submodule)
micropython/    MicroPython (submodule)
src/reader/    Reading engine (opening, typesetting, and drawing books, library, bookmarks, Aozora conversion)
lib/JaBook/    Vertical layout engine (hardware independent)
tools/         SDK setup, fonts, icon generation, pdiy.py
patches/       Patches to the SDK
test/host/     Host tests
```

## Not yet supported

- Python's `socket` and `machine.Pin`. For networking, use `urequests.get` (GET only) or `paperdiy.http_get`
- Rotation (`angle`) in Python's `display.text`. For vertical text, use `paperdiy.text_vertical`
- Loading `.mpy` files and native code (`@micropython.native`)
- Illustration placement, footnote links, and emphasis marks in vertical writing (limits of the vertical layout engine)
- Progressive JPEG in the Images app (the same limit as Badger)
- A password on the Transfer web page. It is meant to be used at home, and only switched on while in use
- Wireless transfer to anywhere other than `/books` (use `tools/pdiy.py push` or an SD card reader)

## Credits

- FreeInk SDK (MIT), M5GFX (MIT), MicroPython (MIT)
- The app structure and controls are based on [Pimoroni Badger 2040](https://github.com/pimoroni/badger2040) (MIT)
- Icons are from [Lucide](https://lucide.dev) (ISC)
