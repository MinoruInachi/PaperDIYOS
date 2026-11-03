#pragma once

// HTTP と HTTPS の取得。
//
// wolfSSL は持ち込まない。ESP-IDF に入っている esp_http_client と、
// そこに同梱の証明書の束（esp_crt_bundle）を使う。追加のライブラリは要らず、
// **サーバの証明書もちゃんと確かめる**。
//
// TLS 1.2 まで。TLS 1.3 しか受けないサーバには届かないが、OPDS を配っている
// ところは 1.2 に対応しているので困らない。

#include <stddef.h>
#include <stdint.h>

namespace paperdiy {

// 受け取ったかたまりを順に渡す。false を返すと途中でやめる。
using HttpChunkFn = bool (*)(void* user, const char* data, size_t len);
// どこまで進んだか（total は分からないとき 0）。
using HttpProgressFn = void (*)(void* user, uint32_t got, uint32_t total);

struct HttpResult {
  int status = 0;         // HTTP のステータス。通信自体が失敗したら 0
  uint32_t received = 0;  // 受け取ったバイト数
  bool ok = false;        // 200 番台で、最後まで受け取れたか
  // 実際に中身が返ってきた URL。行き先が変わって（3xx）いれば、その先。
  // 相対リンクを組み立てる基準は、渡した URL ではなくこちらを使う。
  char finalUrl[224] = {0};
};

// url を取りに行き、届いたそばから sink に渡す。
HttpResult httpGet(const char* url, HttpChunkFn sink, void* user,
                   HttpProgressFn progress = nullptr, void* progressUser = nullptr);

// url の中身を SD カードのファイルに落とす。
HttpResult httpDownload(const char* url, const char* destPath, HttpProgressFn progress = nullptr,
                        void* progressUser = nullptr);

// TLS を張るのに要るヒープ。これを下回っていたら、つなぐ前にあきらめる。
inline constexpr uint32_t kMinTlsHeap = 40000;

}  // namespace paperdiy
