#pragma once

// ブラウザから本を出し入れするための小さなサーバ。
//
// 80 番でページと API、81 番で送信用の WebSocket。中は平文（TLS なし）で、
// 合言葉も付けない。家の中のネットワークで、必要なあいだだけ上げる想定。
//
// つないでいるあいだは loop() を回し続けること（同期式のサーバなので、
// こちらから呼びにいかないと応答しない）。

#include <stdint.h>

namespace paperdiy {

class FileServer {
 public:
  bool begin();
  void loop();
  void end();
  bool running() const { return running_; }

  // 受け取ったファイルの数。0 でなければ本棚を読み直す。
  uint16_t received() const;
  // いま送られている最中のファイル（画面に出す）。空なら待機中。
  const char* current() const;
  uint8_t progress() const;  // 0..100

 private:
  bool running_ = false;
};

}  // namespace paperdiy
