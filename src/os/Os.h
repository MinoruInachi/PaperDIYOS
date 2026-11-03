#pragma once

// PaperDIY OS の本体。Badger OS に当たるところ。
//
// アプリは App を継承して apps/Apps.cpp の一覧に足すだけで、ランチャーに並ぶ。
// OS が受け持つのは次のこと。
//
//   - ボタンの解釈（単押し・長押し・同時押し）。同時押しはランチャーへ戻る
//   - 走っているアプリの記憶。電源を入れ直しても、起きても同じアプリへ戻る
//   - 電池のときの省電力。手が止まったら寝て、A / B で続きから再開する
//   - SD とフォントと電池の窓口
//
// 眠りは 2 種類ある（回路図の電源モード図の L2 DeepSleep とその手前）。
//   deep sleep : ふつうはこちら。起きると起動からやり直すので、状態は NVS と
//                SD に置く。A / B と電源ボタンで起きる
//   light sleep: 時計のように定期的に描き直すアプリ用。RAM もパネルの差分情報も
//                残るので点滅しない。かわりに電流は多い

#include <stdint.h>

#include <BatteryMonitor.h>

#include "Buttons.h"
#include "Gfx.h"

namespace paperdiy {
class FontStore;
}

namespace pdos {

class App {
 public:
  virtual ~App() = default;

  virtual const char* id() const = 0;    // 状態の保存に使う短い名前（15 文字以内）
  virtual const char* name() const = 0;  // ランチャーに出す名前
  virtual const uint8_t* icon() const = 0;

  // アプリに切り替わったとき。resumed は眠りやリセットからの復帰か。
  // wake は起こしたボタン（なければ None）。画面を描いて update するまでがここ。
  virtual void start(bool resumed, Key wake) = 0;
  // ボタンが押された。Chord は OS が受け取るので来ない。
  virtual void key(Key k) = 0;
  // start / tick が setTick で頼んだ時刻に呼ばれる。
  virtual void tick() {}
  // 眠る前とランチャーへ戻る前。状態を保存する。
  virtual void suspend() {}
  // ランチャーへ戻るとき、suspend の後に呼ばれる。メモリを返す。
  virtual void stop() {}

  // 0 以外なら、眠っても定期的に起きて tick したい（秒）。light sleep になる。
  virtual uint32_t wakeIntervalSec() { return 0; }
  // true のあいだは眠らない（通信中など）。
  virtual bool busy() { return false; }
};

// アプリの一覧（apps/Apps.cpp）。ランチャーはこの順に並べる。
extern App* const kApps[];
extern const uint16_t kAppCount;
App* launcherApp();

class Os {
 public:
  void begin();
  void loop();

  Gfx& gfx() { return gfx_; }
  BatteryMonitor& battery() { return battery_; }
  paperdiy::FontStore& fonts();

  bool sdReady() const { return sdReady_; }
  // USB がつながっていれば眠らない。
  bool externalPower();
  // 電池の残量。読めなければ -1。
  int16_t batteryPercent();

  // 手動で別のアプリへ。id が見つからなければ何もしない。
  // "py:/examples/foo.py" のように書くと、その Python スクリプトを走らせる。
  // Python の実行中に呼ぶと、スクリプトを止めてから切り替える。
  void launch(const char* id);
  void goHome();
  App* current() { return current_; }

  // tick を delayMs 後に呼んでもらう。0 で取り消し。
  void setTick(uint32_t delayMs);
  // 無操作の計時をやり直す。長い処理のあとに呼ぶ。
  void touch();

  // 手が止まってから眠るまでの秒数。0 なら眠らない。
  uint16_t idleSleepSec() const { return idleSleepSec_; }
  void setIdleSleepSec(uint16_t sec);

  // wakeSec が 0 でなければ、ボタンに加えてタイマーでも起きる（確かめ物用）。
  void deepSleepNow(uint32_t wakeSec = 0);
  void lightSleepFor(uint32_t sec);
  void powerOff();

  // コンソールからの入力。実ボタンと同じ扱いで届く。
  void injectKey(Key k);
  // キーの待ち行列から 1 つ取る。Python の実行中はこちらで読む。
  Key pollKey();
  // このまえ起こしたボタン。起動して最初のアプリだけが受け取る。
  Key wakeKey() const { return wakeKey_; }
  // 無操作がつづいて電池駆動なら眠る。Python の待ちから呼ばれる。
  void idleCheck();

  // USB シリアルの受信。別タスクが溜めている。
  int usbRead();
  int usbPeek();
  int usbAvailable();
  // Ctrl-C / Ctrl-A が届いた。今のアプリを閉じて REPL へ入る。
  void requestRepl() { replRequested_ = true; }
  bool replRequested() const { return replRequested_; }
  bool inRepl() const { return inRepl_; }
  bool replExitRequested();

  // Python を走らせている間のしるし。A+B はスクリプトを止める合図になる。
  void setPythonActive(bool active);
  // A 長押しをスクリプト自身で受ける（paperdiy.capture_back）。ふつうは OS が「戻る」にする。
  void setBackCaptured(bool captured) { backCaptured_ = captured; }
  // スクリプトの次に開くアプリ。無ければ nullptr。
  const char* takePendingLaunch();
  void setPendingLaunch(const char* id);
  // REPL を閉じる（paperdiy.leave_repl）。
  void leaveRepl() { replExit_ = true; }
  // 止めろと言ったスクリプトが例外を握りつぶしていたら、強制的に止める。
  void repeatStopRequest();

  const char* wakeReason() const;

 private:
  static void buttonTask(void* self);
  void runRepl();
  // Python の実行中に OS が受け取るキーか（A+B と、A 長押し）。
  bool isPythonExitKey(Key k) const { return k == Key::Chord || (k == Key::LongA && !backCaptured_); }
  void stopPython();
  void usbPush(uint8_t c);
  void startApp(App* app, bool resumed, Key wake);
  void leaveCurrent();
  void dispatch(Key k);
  void prepareForSleep();
  void drawFatal(const char* line1, const char* line2);
  void seedSdCard();

  Gfx gfx_;
  BatteryMonitor battery_;
  ButtonDecoder buttons_;
  void* keyQueue_ = nullptr;
  App* current_ = nullptr;
  bool sdReady_ = false;
  uint32_t lastActiveMs_ = 0;
  uint32_t tickAtMs_ = 0;
  bool tickArmed_ = false;
  Key wakeKey_ = Key::None;
  volatile bool pythonActive_ = false;
  volatile bool inRepl_ = false;
  volatile bool replRequested_ = false;
  volatile bool replExit_ = false;
  volatile bool stopRequested_ = false;
  volatile bool backCaptured_ = false;
  uint32_t stopAtMs_ = 0;
  bool hasPendingLaunch_ = false;
  char pendingLaunch_[96] = {0};
  uint8_t* usbRing_ = nullptr;
  volatile uint16_t usbHead_ = 0;
  volatile uint16_t usbTail_ = 0;
  uint16_t idleSleepSec_ = 120;
  uint32_t powerCheckedMs_ = 0;
  bool externalPowerCached_ = true;
  uint8_t wakeKind_ = 0;  // 0:電源投入 1:リセット 2:ボタン 3:タイマー
};

extern Os os;

}  // namespace pdos
