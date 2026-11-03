#pragma once

// SD の .py をアプリとして走らせる。Badger の badger_os.launch に当たる。

#include "os/Os.h"

namespace pdos {

class PythonApp : public App {
 public:
  const char* id() const override { return id_; }
  const char* name() const override { return scriptName(); }
  const uint8_t* icon() const override;

  void setScript(const char* path);
  void start(bool resumed, Key wake) override;
  void key(Key k) override;
  void tick() override;

 private:
  const char* scriptName() const;
  void showError();

  char id_[96] = {0};
  bool showingError_ = false;
  char* error_ = nullptr;
};

PythonApp& pythonApp();

// Python の作業領域（PSRAM）。一度取ったら返さない。
void* pythonHeap(size_t* size);

}  // namespace pdos
