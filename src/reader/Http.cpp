#include "Http.h"

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <string.h>

#include <SDCardManager.h>

#include "Opds.h"  // resolveUrl（行き先が変わったときの組み立て）

namespace paperdiy {
namespace {

SDCardManager& sd() { return SDCardManager::getInstance(); }

constexpr uint32_t kTimeoutMs = 30000;
constexpr size_t kChunk = 2048;
constexpr uint8_t kMaxRedirects = 5;

bool isHttps(const char* url) { return strncmp(url, "https://", 8) == 0; }

// 行き先が変わったことを表すステータスか。
bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// 3xx のときの行き先を受け取るための箱。
//
// esp_http_client_get_header() はこちらが**送った**ヘッダを返すもので、
// 相手が返してきたヘッダは取れない。応答ヘッダはイベントで流れてくるので、
// そこで Location だけ写し取る。
struct HeaderCatch {
  char location[224] = {0};
};

esp_err_t onHttpEvent(esp_http_client_event_t* evt) {
  if (evt == nullptr || evt->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
  HeaderCatch* box = static_cast<HeaderCatch*>(evt->user_data);
  if (box == nullptr || evt->header_key == nullptr) return ESP_OK;
  if (strcasecmp(evt->header_key, "Location") == 0 && evt->header_value != nullptr) {
    snprintf(box->location, sizeof(box->location), "%s", evt->header_value);
  }
  return ESP_OK;
}

// ファイルへ落とすときの受け皿。
struct FileSink {
  FsFile file;
  bool failed = false;
};

bool writeToFile(void* user, const char* data, size_t len) {
  FileSink* sink = static_cast<FileSink*>(user);
  if (sink->file.write(data, len) != static_cast<int>(len)) {
    sink->failed = true;
    return false;
  }
  return true;
}

}  // namespace

HttpResult httpGet(const char* url, HttpChunkFn sink, void* user, HttpProgressFn progress,
                   void* progressUser) {
  HttpResult result;
  if (url == nullptr || url[0] == '\0' || sink == nullptr) return result;

  // 行き先が変わったら（3xx）、Location を見て自分で追い直す。
  //
  // esp_http_client の自動追跡は esp_http_client_perform() のときだけ働く。
  // ここは応答を溜めずに流したいので open/fetch_headers/read を直に使っており、
  // その経路では効かない。それに、http から https へ飛ばされたときは
  // 証明書の束を張り直す必要があるので、どのみち作り直すことになる。
  char current[sizeof(result.finalUrl)];
  snprintf(current, sizeof(current), "%s", url);

  for (uint8_t hop = 0;; ++hop) {
    // TLS はヒープを一気に食う。足りないまま入ると途中で落ちるので、先に断る。
    if (isHttps(current) && ESP.getFreeHeap() < kMinTlsHeap) {
      Serial.printf("[http] ヒープが足りません（%u KB）\n",
                    static_cast<unsigned>(ESP.getFreeHeap() / 1024));
      return result;
    }

    HeaderCatch caught;
    esp_http_client_config_t config = {};
    config.url = current;
    config.timeout_ms = kTimeoutMs;
    config.buffer_size = kChunk;
    config.buffer_size_tx = 1024;
    config.disable_auto_redirect = true;  // 追うのはこちらの仕事
    config.event_handler = onHttpEvent;
    config.user_data = &caught;
    if (isHttps(current)) {
      // ESP-IDF に入っている証明書の束で、相手を確かめる。
      config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) return result;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      Serial.printf("[http] つながりません: %s\n", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return result;
    }

    const int64_t total = esp_http_client_fetch_headers(client);
    result.status = esp_http_client_get_status_code(client);

    if (isRedirect(result.status)) {
      // 相対で返してくるサーバもあるので、いまの URL を基準に組み立てる。
      char target[sizeof(current)] = {0};
      if (caught.location[0] != '\0') {
        resolveUrl(current, caught.location, target, sizeof(target));
      }
      esp_http_client_close(client);
      esp_http_client_cleanup(client);

      if (target[0] == '\0') {
        Serial.printf("[http] %d なのに行き先がありません: %s\n", result.status, current);
        return result;
      }
      if (hop >= kMaxRedirects) {
        Serial.printf("[http] 行き先が変わりすぎです: %s\n", target);
        return result;
      }
      Serial.printf("[http] %d → %s\n", result.status, target);
      snprintf(current, sizeof(current), "%s", target);
      continue;
    }

    if (result.status < 200 || result.status >= 300) {
      Serial.printf("[http] %d が返りました: %s\n", result.status, current);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return result;
    }

    snprintf(result.finalUrl, sizeof(result.finalUrl), "%s", current);

    static char buf[kChunk];
    bool aborted = false;
    while (true) {
      const int got = esp_http_client_read(client, buf, sizeof(buf));
      if (got < 0) {
        aborted = true;
        break;
      }
      if (got == 0) {
        // 全部読み切ったか、向こうが切ったか。
        if (esp_http_client_is_complete_data_received(client)) break;
        if (esp_http_client_is_chunked_response(client) && total <= 0) break;
        break;
      }
      result.received += static_cast<uint32_t>(got);
      if (!sink(user, buf, static_cast<size_t>(got))) {
        aborted = true;
        break;
      }
      if (progress != nullptr) {
        progress(progressUser, result.received, total > 0 ? static_cast<uint32_t>(total) : 0);
      }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    result.ok = !aborted && result.received > 0;
    return result;
  }
}

HttpResult httpDownload(const char* url, const char* destPath, HttpProgressFn progress,
                        void* progressUser) {
  HttpResult result;
  if (destPath == nullptr) return result;

  FileSink sink;
  sink.file = sd().open(destPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!sink.file) {
    Serial.printf("[http] 書き込めません: %s\n", destPath);
    return result;
  }

  result = httpGet(url, writeToFile, &sink, progress, progressUser);
  sink.file.close();

  if (!result.ok || sink.failed) {
    sd().remove(destPath);  // 途中まで落ちたものは残さない
    result.ok = false;
  }
  return result;
}

}  // namespace paperdiy
