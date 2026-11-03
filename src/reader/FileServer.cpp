#include "FileServer.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <string.h>

#include <SDCardManager.h>

#include "Config.h"
#include "html/FilesPage.h"

namespace paperdiy {
namespace {

SDCardManager& sd() { return SDCardManager::getInstance(); }

WebServer* gHttp = nullptr;
WebSocketsServer* gSocket = nullptr;

// 送信中の 1 本。同時に受けるのは 1 本だけ（2 本目が来たら断る）。
struct Upload {
  FsFile file;
  char path[224] = {0};
  uint32_t expected = 0;
  uint32_t got = 0;
  int8_t client = -1;  // -1 = 誰も送っていない
  bool active = false;
};
Upload gUpload;
uint16_t gReceived = 0;

// /books の外へは出さない。".." を含む道はすべて断る。
bool safePath(const char* path) {
  if (path == nullptr || path[0] == '\0') return false;
  if (strncmp(path, kBooksDir, strlen(kBooksDir)) != 0) return false;
  if (strstr(path, "..") != nullptr) return false;
  return true;
}

void sendText(int8_t client, const char* text) {
  if (gSocket != nullptr && client >= 0) {
    gSocket->sendTXT(static_cast<uint8_t>(client), text);
  }
}

void abortUpload() {
  if (gUpload.file) {
    gUpload.file.close();
    sd().remove(gUpload.path);  // 途中まで書いたものは残さない
  }
  gUpload = Upload{};
}

// --- HTTP -------------------------------------------------------------------

void handleRoot() {
  gHttp->sendHeader("Content-Encoding", "gzip");
  gHttp->send_P(200, "text/html; charset=utf-8",
                reinterpret_cast<const char*>(FilesPageGzip), FilesPageGzipSize);
}

// JSON を自前で組む。文字列に入りうるのは " と \ と制御文字だけなので、
// そこだけ逃がせばよい（ArduinoJson を持ち込むほどの用事ではない）。
void appendEscaped(String& out, const char* s) {
  for (const char* p = s; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
}

void handleList() {
  String dir = gHttp->hasArg("dir") ? gHttp->arg("dir") : String(kBooksDir);
  if (!safePath(dir.c_str())) {
    gHttp->send(400, "text/plain", "bad path");
    return;
  }

  FsFile folder = sd().open(dir.c_str(), O_RDONLY);
  if (!folder) {
    gHttp->send(404, "text/plain", "not found");
    return;
  }

  String out = "{\"items\":[";
  FsFile child;
  char name[160];
  bool first = true;
  while (child.openNext(&folder, O_RDONLY)) {
    const bool isDir = child.isDir();
    const uint32_t size = isDir ? 0 : child.size();
    const bool named = child.getName(name, sizeof(name));
    child.close();
    if (!named || name[0] == '.') continue;

    if (!first) out += ',';
    first = false;
    out += "{\"name\":\"";
    appendEscaped(out, name);
    out += "\",\"dir\":";
    out += isDir ? "true" : "false";
    out += ",\"size\":";
    out += size;
    out += '}';
  }
  folder.close();
  out += "]}";
  gHttp->send(200, "application/json; charset=utf-8", out);
}

void handleGet() {
  const String path = gHttp->arg("path");
  if (!safePath(path.c_str())) {
    gHttp->send(400, "text/plain", "bad path");
    return;
  }
  FsFile f = sd().open(path.c_str(), O_RDONLY);
  if (!f || f.isDir()) {
    if (f) f.close();
    gHttp->send(404, "text/plain", "not found");
    return;
  }
  const char* slash = strrchr(path.c_str(), '/');
  String disp = "attachment; filename=\"";
  disp += (slash != nullptr ? slash + 1 : path.c_str());
  disp += '"';
  gHttp->sendHeader("Content-Disposition", disp);

  // streamFile は使えない（中で file.name() を呼ぶが、SdFat では
  // getName(buf, len) に置き換わっていてコンパイルが通らない）。自分で流す。
  gHttp->setContentLength(f.size());
  gHttp->send(200, "application/octet-stream", "");
  uint8_t buf[1024];
  WiFiClient client = gHttp->client();
  while (client.connected()) {
    const int got = f.read(buf, sizeof(buf));
    if (got <= 0) break;
    if (client.write(buf, static_cast<size_t>(got)) != static_cast<size_t>(got)) break;
  }
  f.close();
}

void handleDelete() {
  const String path = gHttp->arg("path");
  if (!safePath(path.c_str())) {
    gHttp->send(400, "text/plain", "bad path");
    return;
  }
  const bool ok = sd().remove(path.c_str()) || sd().removeDir(path.c_str());
  gHttp->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "failed");
}

// --- WebSocket（送信） ------------------------------------------------------
//
//   →  "START:<名前>:<バイト数>:<入れる場所>"
//   ←  "READY"                （断るときは "ERROR:<わけ>"）
//   →  中身（分割して何度でも）
//   ←  "PROGRESS:<受けた>:<全部>"
//   ←  "DONE"

void startUpload(uint8_t client, const char* payload) {
  if (gUpload.active) {
    sendText(static_cast<int8_t>(client), "ERROR:ほかのファイルを受けています");
    return;
  }

  // "START:" のあとを、名前 : サイズ : 置き場所 に分ける。
  // 名前にコロンが入ることはある（FAT では使えないが、送る側は知らない）ので、
  // 後ろから 2 つのコロンで切る。
  const char* body = payload + 6;
  const char* lastColon = strrchr(body, ':');
  if (lastColon == nullptr) {
    sendText(static_cast<int8_t>(client), "ERROR:読めない指示です");
    return;
  }
  char dir[192];
  snprintf(dir, sizeof(dir), "%s", lastColon + 1);

  char head[288];
  const size_t headLen = static_cast<size_t>(lastColon - body);
  if (headLen >= sizeof(head)) {
    sendText(static_cast<int8_t>(client), "ERROR:名前が長すぎます");
    return;
  }
  memcpy(head, body, headLen);
  head[headLen] = '\0';

  char* sizeColon = strrchr(head, ':');
  if (sizeColon == nullptr) {
    sendText(static_cast<int8_t>(client), "ERROR:読めない指示です");
    return;
  }
  *sizeColon = '\0';
  const char* name = head;
  const uint32_t size = static_cast<uint32_t>(strtoul(sizeColon + 1, nullptr, 10));

  if (name[0] == '\0' || strchr(name, '/') != nullptr || strstr(name, "..") != nullptr) {
    sendText(static_cast<int8_t>(client), "ERROR:使えない名前です");
    return;
  }
  if (!safePath(dir)) {
    sendText(static_cast<int8_t>(client), "ERROR:そこには置けません");
    return;
  }

  snprintf(gUpload.path, sizeof(gUpload.path), "%s/%s", dir, name);
  if (sd().exists(gUpload.path)) {
    sendText(static_cast<int8_t>(client), "ERROR:同じ名前のものがあります");
    gUpload.path[0] = '\0';
    return;
  }

  gUpload.file = sd().open(gUpload.path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!gUpload.file) {
    sendText(static_cast<int8_t>(client), "ERROR:書き込めません");
    gUpload.path[0] = '\0';
    return;
  }
  gUpload.expected = size;
  gUpload.got = 0;
  gUpload.client = static_cast<int8_t>(client);
  gUpload.active = true;

  sendText(gUpload.client, "READY");
  // 中身のないファイルは、そこで終わり。
  if (size == 0) {
    gUpload.file.close();
    ++gReceived;
    sendText(gUpload.client, "DONE");
    gUpload = Upload{};
  }
}

void feedUpload(uint8_t client, const uint8_t* data, size_t len) {
  if (!gUpload.active || gUpload.client != static_cast<int8_t>(client)) return;

  if (gUpload.got + len > gUpload.expected) {
    sendText(gUpload.client, "ERROR:言われたより多く来ました");
    abortUpload();
    return;
  }
  if (gUpload.file.write(data, len) != static_cast<int>(len)) {
    sendText(gUpload.client, "ERROR:書き込めません（空きが足りないかも）");
    abortUpload();
    return;
  }
  gUpload.got += len;

  if (gUpload.got >= gUpload.expected) {
    gUpload.file.close();
    ++gReceived;
    sendText(gUpload.client, "DONE");
    gUpload = Upload{};
    return;
  }
  char msg[64];
  snprintf(msg, sizeof(msg), "PROGRESS:%lu:%lu", static_cast<unsigned long>(gUpload.got),
           static_cast<unsigned long>(gUpload.expected));
  sendText(gUpload.client, msg);
}

void onSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_DISCONNECTED:
      // 送っている途中で切れた相手のぶんだけ片づける。
      if (gUpload.active && gUpload.client == static_cast<int8_t>(client)) abortUpload();
      break;
    case WStype_TEXT:
      if (len >= 6 && strncmp(reinterpret_cast<const char*>(payload), "START:", 6) == 0) {
        payload[len] = '\0';
        startUpload(client, reinterpret_cast<const char*>(payload));
      }
      break;
    case WStype_BIN:
      feedUpload(client, payload, len);
      break;
    default:
      break;
  }
}

}  // namespace

bool FileServer::begin() {
  if (running_) return true;
  gReceived = 0;
  gUpload = Upload{};

  gHttp = new WebServer(80);
  if (gHttp == nullptr) return false;
  gHttp->on("/", handleRoot);
  gHttp->on("/api/list", handleList);
  gHttp->on("/api/get", handleGet);
  gHttp->on("/api/delete", HTTP_POST, handleDelete);
  gHttp->onNotFound(handleRoot);  // どこを叩かれてもページを返す
  gHttp->begin();

  gSocket = new WebSocketsServer(81);
  if (gSocket == nullptr) {
    delete gHttp;
    gHttp = nullptr;
    return false;
  }
  gSocket->begin();
  gSocket->onEvent(onSocketEvent);

  running_ = true;
  return true;
}

void FileServer::loop() {
  if (!running_) return;
  gHttp->handleClient();
  gSocket->loop();
}

void FileServer::end() {
  if (!running_) return;
  abortUpload();
  if (gSocket != nullptr) {
    gSocket->close();
    delete gSocket;
    gSocket = nullptr;
  }
  if (gHttp != nullptr) {
    gHttp->stop();
    delete gHttp;
    gHttp = nullptr;
  }
  running_ = false;
}

uint16_t FileServer::received() const { return gReceived; }

const char* FileServer::current() const {
  if (!gUpload.active) return "";
  const char* slash = strrchr(gUpload.path, '/');
  return slash != nullptr ? slash + 1 : gUpload.path;
}

uint8_t FileServer::progress() const {
  if (!gUpload.active || gUpload.expected == 0) return 0;
  return static_cast<uint8_t>((static_cast<uint64_t>(gUpload.got) * 100) / gUpload.expected);
}

}  // namespace paperdiy
