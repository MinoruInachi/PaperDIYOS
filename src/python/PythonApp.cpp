#include "PythonApp.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

#include <pd_bridge.h>

#include "os/I18n.h"
#include "os/Icons.h"
#include "os/Ui.h"
#include "pd_interrupt.h"

namespace pdos {
namespace {

constexpr size_t kHeapSize = 2u * 1024 * 1024;
constexpr size_t kErrorCap = 2048;
constexpr uint32_t kStartDelayMs = 1;

PythonApp gApp;

}  // namespace

PythonApp& pythonApp() { return gApp; }

void* pythonHeap(size_t* size) {
  static void* heap = nullptr;
  if (heap == nullptr) heap = heap_caps_malloc(kHeapSize, MALLOC_CAP_SPIRAM);
  *size = kHeapSize;
  return heap;
}

void pythonRunRepl() {
  size_t size = 0;
  void* heap = pythonHeap(&size);
  if (heap != nullptr) pd_py_run_repl(heap, size);
}

const uint8_t* PythonApp::icon() const { return icons::code; }

const char* PythonApp::scriptName() const {
  const char* slash = strrchr(id_, '/');
  return slash != nullptr ? slash + 1 : id_;
}

void PythonApp::setScript(const char* path) { snprintf(id_, sizeof(id_), "py:%s", path); }

void PythonApp::start(bool, Key) {
  showingError_ = false;
  // 走らせるのは loop に戻ってから。startApp の途中で長く止まらないように。
  os.setTick(kStartDelayMs);
}

void PythonApp::tick() {
  size_t size = 0;
  void* heap = pythonHeap(&size);
  if (heap == nullptr) {
    ui::message(TR("メモリが足りません", "Out of memory"));
    showingError_ = true;
    return;
  }
  if (error_ == nullptr) error_ = static_cast<char*>(heap_caps_malloc(kErrorCap, MALLOC_CAP_SPIRAM));
  error_[0] = '\0';

  // Badger と同じく、スクリプトは真っ白な紙から始まる。
  os.gfx().clear();
  os.gfx().setPen(0);
  os.setPythonActive(true);
  const int rc = pd_py_run_file(id_ + 3, heap, size, error_, kErrorCap);
  os.setPythonActive(false);
  while (os.pollKey() != Key::None) {
  }
  os.touch();

  if (os.replRequested()) return;  // loop が REPL へ入れる
  const char* next = os.takePendingLaunch();
  if (next != nullptr) {
    char target[96];
    snprintf(target, sizeof(target), "%s", next);
    os.launch(target);
    return;
  }
  if (rc == PD_RUN_ERROR) {
    showError();
    return;
  }
  os.goHome();
}

void PythonApp::showError() {
  showingError_ = true;
  Gfx& g = os.gfx();
  g.clear();
  ui::header(scriptName());
  g.setPen(0);
  g.textWrap(error_, ui::kMargin, ui::kHeaderH + 44, Gfx::W - ui::kMargin * 2, 20, 28,
             (Gfx::H - ui::kHeaderH - ui::kFooterH - 60) / 28);
  ui::footer(TR("ボタンでランチャーへ", "Press a button for launcher"));
  g.update(Update::Text);
}

void PythonApp::key(Key) {
  if (showingError_) os.goHome();
}

}  // namespace pdos
