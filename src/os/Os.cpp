#include "Os.h"

#include <Arduino.h>
#include <BoardPaperDiy.h>
#include <EInkDisplay.h>
#include <SDCardManager.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "Console.h"
#include "I18n.h"
#include "Net.h"
#include "Sample.h"
#include "Store.h"
#include "python/PythonApp.h"
#include "python/pd_interrupt.h"
#include <pd_bridge.h>
#include "reader/FontStore.h"

namespace pdos {

Os os;

namespace {

constexpr uint8_t kPinA = PAPERDIY_KEY_A;
constexpr uint8_t kPinB = PAPERDIY_KEY_B;
constexpr uint32_t kPollMs = 2;
constexpr uint16_t kUsbRingSize = 8192;
constexpr uint32_t kStopGraceMs = 1000;
constexpr int kCtrlA = 0x01;
constexpr int kCtrlC = 0x03;
constexpr uint32_t kPowerCheckMs = 5000;
constexpr uint32_t kClockIdleMs = 5000;  // 定期描画のアプリは早めに眠る

// USB CDC がつながるのを待つ上限。起動ログの頭を失わないための待ち。
constexpr uint32_t kSerialWaitMs = 600;

constexpr const char* kKeyRunning = "os.running";
constexpr const char* kKeyIdle = "os.idle";
constexpr const char* kSeededPath = "/.paperdiy/seeded2";

struct RunningState {
  char id[96] = {0};
};

EInkDisplay gDisplay(BoardConfig::ACTIVE.display.sclk, BoardConfig::ACTIVE.display.mosi, BoardConfig::ACTIVE.display.cs,
                     BoardConfig::ACTIVE.display.dc, BoardConfig::ACTIVE.display.rst, BoardConfig::ACTIVE.display.busy);
paperdiy::FontStore gFonts;

bool pressed(uint8_t pin) { return digitalRead(pin) == LOW; }

App* findApp(const char* id) {
  if (strncmp(id, "py:", 3) == 0) {
    PythonApp& py = pythonApp();
    py.setScript(id + 3);
    return &py;
  }
  if (strcmp(launcherApp()->id(), id) == 0) return launcherApp();
  for (uint16_t i = 0; i < kAppCount; ++i) {
    if (strcmp(kApps[i]->id(), id) == 0) return kApps[i];
  }
  return nullptr;
}

}  // namespace

paperdiy::FontStore& Os::fonts() { return gFonts; }

void Os::drawFatal(const char* line1, const char* line2) {
  gfx_.clear();
  gfx_.setPen(0);
  gfx_.text(line1, 24, 400, 24);
  gfx_.text(line2, 24, 450, 16);
  gfx_.update(Update::Quality);
}

void Os::seedSdCard() {
  auto& sd = SDCardManager::getInstance();
  if (sd.exists(kSeededPath)) return;
  sample::write();
  sd.mkdir("/.paperdiy", true);
  FsFile f = sd.open(kSeededPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (f) f.close();
}

void Os::begin() {
  // 起こされた理由は、何より先に控える。
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint64_t wakePins = 0;
  if (cause == ESP_SLEEP_WAKEUP_EXT1) wakePins = esp_sleep_get_ext1_wakeup_status();

  // pdiy.py push の 1 行が余裕で入るように、受信バッファを広げておく。
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  // ボタンで起きたときは待たない。電池のときは USB がつながらないので、
  // 待つと再開がそのぶん遅くなる。
  if (cause != ESP_SLEEP_WAKEUP_EXT1) {
    for (uint32_t i = 0; i < kSerialWaitMs / 10 && !Serial; ++i) delay(10);
  }
  Serial.setTimeout(2000);
  Serial.println("\nPaperDIY OS");

  pinMode(kPinA, INPUT_PULLUP);
  pinMode(kPinB, INPUT_PULLUP);

  // deep sleep 中は RTC IO として押さえていたので、ふつうの GPIO へ戻す。
  rtc_gpio_deinit(static_cast<gpio_num_t>(kPinA));
  rtc_gpio_deinit(static_cast<gpio_num_t>(kPinB));
  pinMode(kPinA, INPUT_PULLUP);
  pinMode(kPinB, INPUT_PULLUP);

  BoardPaperDiy::applyBootPower();

  gDisplay.begin();
  gfx_.begin();
  gfx_.clear();
  Serial.printf("[boot] canvas=%p psram=%u\n", gfx_.canvas(), ESP.getFreePsram());

  sdReady_ = SDCardManager::getInstance().begin();
  Serial.printf("[boot] sd=%d\n", sdReady_);
  if (sdReady_) seedSdCard();

  if (gFonts.begin()) {
    gfx_.setFonts(&gFonts.chain());
    Serial.printf("[boot] font=%s faces=%u\n", gFonts.sourceName(), gFonts.faceCount());
  } else {
    Serial.println("[boot] no font");
    drawFatal("NO JAPANESE FONT", "run: tools/flash_font.sh assets/font.img");
  }

  uint16_t idle = idleSleepSec_;
  i18n::load();
  if (store::load(kKeyIdle, idle)) idleSleepSec_ = idle;

  net::applyTimezone();

  // 起こしたボタンを読み解く。すぐ離されていれば、もう GPIO には残っていない。
  const bool wokeA = (wakePins & (1ULL << kPinA)) != 0;
  const bool wokeB = (wakePins & (1ULL << kPinB)) != 0;
  const bool heldA = pressed(kPinA), heldB = pressed(kPinB);
  buttons_.seed(heldA, heldB, millis());

  Key wake = Key::None;
  if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    wakeKind_ = 2;
    if (wokeA && wokeB) {
      wake = Key::Chord;
    } else if (!heldA && !heldB) {
      wake = wokeA ? Key::A : Key::B;
    }
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    wakeKind_ = 3;
  } else {
    wakeKind_ = esp_reset_reason() == ESP_RST_POWERON ? 0 : 1;
  }

  // 同時押しのまま起きたら、Badger の A+C と同じくランチャーへ。
  if (heldA && heldB) wake = Key::Chord;

  // キーと USB を見張るタスク。描画中（loop が止まっている 0.5〜1.4 秒）に
  // 押された分も落とさず、Python の実行中でも A 長押し、A+B、Ctrl-C を受けられる。
  usbRing_ = static_cast<uint8_t*>(heap_caps_malloc(kUsbRingSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  keyQueue_ = xQueueCreate(8, sizeof(Key));
  xTaskCreatePinnedToCore(buttonTask, "buttons", 3072, this, 2, nullptr, 0);

  RunningState running;
  App* app = nullptr;
  if (wake != Key::Chord && store::load(kKeyRunning, running)) app = findApp(running.id);
  if (app == nullptr) app = launcherApp();

  wakeKey_ = wake == Key::Chord ? Key::None : wake;
  Serial.printf("[boot] wake=%s app=%s\n", wakeReason(), app->id());
  lastActiveMs_ = millis();
  externalPowerCached_ = externalPower();
  startApp(app, wake != Key::Chord, wakeKey_);
  Serial.println("[boot] ready");
}

void Os::buttonTask(void* self) {
  Os* o = static_cast<Os*>(self);
  for (;;) {
    const Key k = o->buttons_.update(pressed(kPinA), pressed(kPinB), millis());
    if (o->pythonActive_ && o->isPythonExitKey(k)) {
      o->stopPython();
    } else if (k != Key::None) {
      xQueueSend(static_cast<QueueHandle_t>(o->keyQueue_), &k, 0);
    }
    while (Serial.available() > 0) {
      const int c = Serial.read();
      if (c < 0) break;
      o->usbPush(static_cast<uint8_t>(c));
    }
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }
}

void Os::stopPython() {
  if (inRepl_) {
    replExit_ = true;
    pyInterruptIfRunning();
  } else {
    setPendingLaunch(launcherApp()->id());
    stopRequested_ = true;
    pyInterrupt();
  }
}

void Os::repeatStopRequest() {
  if (!stopRequested_) return;
  const uint32_t now = millis();
  if (stopAtMs_ == 0) stopAtMs_ = now == 0 ? 1 : now;
  // しばらく待っても止まらなければ、捕まえられない中断で止める。
  if (now - stopAtMs_ >= kStopGraceMs) {
    stopRequested_ = false;
    pd_py_abort();
  }
}

void Os::usbPush(uint8_t c) {
  // 実行中のスクリプトへの Ctrl-C。バッファには積まない。
  if (pythonActive_ && c == pyInterruptChar()) {
    if (!inRepl_) {
      replRequested_ = true;
      stopRequested_ = true;
    }
    pyInterrupt();
    return;
  }
  const uint16_t next = static_cast<uint16_t>((usbHead_ + 1) % kUsbRingSize);
  if (next == usbTail_ || usbRing_ == nullptr) return;
  usbRing_[usbHead_] = c;
  usbHead_ = next;
}

int Os::usbRead() {
  if (usbTail_ == usbHead_) return -1;
  const uint8_t c = usbRing_[usbTail_];
  usbTail_ = static_cast<uint16_t>((usbTail_ + 1) % kUsbRingSize);
  return c;
}

int Os::usbPeek() { return usbTail_ == usbHead_ ? -1 : usbRing_[usbTail_]; }

int Os::usbAvailable() {
  return (usbHead_ + kUsbRingSize - usbTail_) % kUsbRingSize;
}

Key Os::pollKey() {
  Key k = Key::None;
  if (keyQueue_ != nullptr) xQueueReceive(static_cast<QueueHandle_t>(keyQueue_), &k, 0);
  return k;
}

const char* Os::wakeReason() const {
  switch (wakeKind_) {
    case 1: return TR("リセット", "Reset");
    case 2: return TR("ボタン", "Button");
    case 3: return TR("タイマー", "Timer");
    default: return TR("電源投入", "Power on");
  }
}

void Os::setPythonActive(bool active) {
  pythonActive_ = active;
  backCaptured_ = false;
  stopRequested_ = false;
  stopAtMs_ = 0;
}

bool Os::replExitRequested() {
  // USB を抜いたら、誰も打てないので閉じる。
  if (inRepl_ && !Serial) replExit_ = true;
  return replExit_;
}

void Os::setPendingLaunch(const char* id) {
  snprintf(pendingLaunch_, sizeof(pendingLaunch_), "%s", id);
  hasPendingLaunch_ = true;
}

const char* Os::takePendingLaunch() {
  if (!hasPendingLaunch_) return nullptr;
  hasPendingLaunch_ = false;
  return pendingLaunch_;
}

void Os::idleCheck() {
  if (idleSleepSec_ == 0 || externalPower()) return;
  if (millis() - lastActiveMs_ >= idleSleepSec_ * 1000UL) deepSleepNow();
}

void Os::runRepl() {
  replRequested_ = false;
  replExit_ = false;
  App* app = current_;
  leaveCurrent();
  gfx_.clear();
  gfx_.setPen(0);
  gfx_.textCentered("MicroPython REPL", Gfx::W / 2, Gfx::H / 2 - 20, 36);
  gfx_.textCentered(TR("USB からつないでいます", "Connected over USB"), Gfx::W / 2, Gfx::H / 2 + 28, 24);
  gfx_.textCentered(TR("A 長押しで戻ります", "Hold A to go back"), Gfx::W / 2, Gfx::H / 2 + 72, 24);
  // mpremote や Thonny は応答を長くは待たないので、速い更新で済ませる。
  gfx_.update(Update::Fast);
  inRepl_ = true;
  backCaptured_ = false;
  pythonActive_ = true;
  pythonRunRepl();
  pythonActive_ = false;
  inRepl_ = false;
  replExit_ = false;
  while (pollKey() != Key::None) {
  }
  startApp(app, true, Key::None);
}

bool Os::externalPower() {
  const uint32_t now = millis();
  if (powerCheckedMs_ == 0 || now - powerCheckedMs_ >= kPowerCheckMs) {
    powerCheckedMs_ = now == 0 ? 1 : now;
    bool known = false;
    const bool present = battery_.isExternalPowerPresent(&known);
    // 読めないときは「つながっている」側に倒す。寝て起きなくなるより安全。
    externalPowerCached_ = known ? present : true;
  }
  return externalPowerCached_;
}

int16_t Os::batteryPercent() {
  uint16_t pct = 0;
  return battery_.readPercentageChecked(pct) ? static_cast<int16_t>(pct) : -1;
}

void Os::startApp(App* app, bool resumed, Key wake) {
  current_ = app;
  RunningState running;
  snprintf(running.id, sizeof(running.id), "%s", app->id());
  // 起こしたボタンは最初のアプリにだけ渡す。
  if (wake == Key::None) wakeKey_ = Key::None;
  store::save(kKeyRunning, running);
  tickArmed_ = false;
  app->start(resumed, wake);
  touch();
}

void Os::leaveCurrent() {
  if (current_ == nullptr) return;
  current_->suspend();
  current_->stop();
}

void Os::launch(const char* id) {
  if (pythonActive_) {
    setPendingLaunch(id);
    if (inRepl_) {
      replExit_ = true;
      pyInterruptIfRunning();
    } else {
      stopRequested_ = true;
      pyInterrupt();
    }
    return;
  }
  App* app = findApp(id);
  if (app == nullptr) return;
  leaveCurrent();
  startApp(app, false, Key::None);
}

void Os::goHome() { launch(launcherApp()->id()); }

void Os::setTick(uint32_t delayMs) {
  tickArmed_ = delayMs != 0;
  tickAtMs_ = millis() + delayMs;
}

void Os::touch() { lastActiveMs_ = millis(); }

void Os::setIdleSleepSec(uint16_t sec) {
  idleSleepSec_ = sec;
  store::save(kKeyIdle, sec);
}

void Os::injectKey(Key k) {
  if (pythonActive_ && isPythonExitKey(k)) {
    stopPython();
    return;
  }
  xQueueSend(static_cast<QueueHandle_t>(keyQueue_), &k, 0);
}

void Os::dispatch(Key k) {
  touch();
  if (k == Key::Chord) {
    if (current_ != launcherApp()) goHome();
    return;
  }
  // 次のキーが待っていれば、この操作の描画は飛ばす。連打しても指を止めれば
  // 画面もすぐ止まり、カーソルは押した回数ぶん進んだ位置に出る。
  gfx_.setDeferFast(uxQueueMessagesWaiting(static_cast<QueueHandle_t>(keyQueue_)) > 0);
  if (current_ != nullptr) current_->key(k);
  gfx_.setDeferFast(false);
  if (uxQueueMessagesWaiting(static_cast<QueueHandle_t>(keyQueue_)) == 0) gfx_.flushDeferred();
  touch();
}

void Os::prepareForSleep() {
  if (current_ != nullptr) current_->suspend();
  gDisplay.deepSleep();
  net::stop();
}

void Os::deepSleepNow(uint32_t wakeSec) {
  prepareForSleep();
  BoardPaperDiy::setEpdRail(false);
  Serial.println("[power] deep sleep");
  Serial.flush();

  // 押したまま眠るとその場で起きるので、離されるのを待つ。
  while (pressed(kPinA) || pressed(kPinB)) delay(10);
  delay(30);

  rtc_gpio_pullup_en(static_cast<gpio_num_t>(kPinA));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(kPinA));
  rtc_gpio_pullup_en(static_cast<gpio_num_t>(kPinB));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(kPinB));
  esp_sleep_enable_ext1_wakeup((1ULL << kPinA) | (1ULL << kPinB), ESP_EXT1_WAKEUP_ANY_LOW);
  if (wakeSec != 0) esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(wakeSec) * 1000000ULL);
  esp_deep_sleep_start();
}

void Os::lightSleepFor(uint32_t sec) {
  // パネルの電源は Bus_EPD が描画のたびに上げ下げしているので、ここでは触らない。
  gpio_wakeup_enable(static_cast<gpio_num_t>(kPinA), GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(static_cast<gpio_num_t>(kPinB), GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sec) * 1000000ULL);
  Serial.flush();
  esp_light_sleep_start();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(static_cast<gpio_num_t>(kPinA));
  gpio_wakeup_disable(static_cast<gpio_num_t>(kPinB));
  pinMode(kPinA, INPUT_PULLUP);
  pinMode(kPinB, INPUT_PULLUP);
}

void Os::powerOff() {
  prepareForSleep();
  BoardPaperDiy::setEpdRail(false);
  BoardPaperDiy::powerOff();
  // USB がつながっていると切れない。そのときは deep sleep で待つ。
  delay(800);
  deepSleepNow();
}

void Os::loop() {
  console::poll();

  if (replRequested_) {
    runRepl();
    return;
  }

  Key queued = pollKey();
  if (queued != Key::None) dispatch(queued);

  if (tickArmed_ && static_cast<int32_t>(millis() - tickAtMs_) >= 0) {
    tickArmed_ = false;
    if (current_ != nullptr) current_->tick();
  }

  // 手が止まったら眠る。USB がつながっていれば眠らない（Badger と同じ）。
  if (buttons_.anyDown() || current_ == nullptr || current_->busy()) return;
  if (externalPower()) return;
  const uint32_t idle = millis() - lastActiveMs_;
  const uint32_t interval = current_->wakeIntervalSec();
  if (interval != 0) {
    if (idle >= kClockIdleMs) lightSleepFor(interval);
    return;
  }
  if (idleSleepSec_ != 0 && idle >= idleSleepSec_ * 1000UL) deepSleepNow();
}

}  // namespace pdos
