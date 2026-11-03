#include "Console.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <mbedtls/base64.h>
#include <string.h>

#include "Os.h"

namespace pdos {
namespace console {
namespace {

char gLine[256];
size_t gLen = 0;

// 画面を論理座標（540x960）で 4bit に詰めて base64 で送る。
void cmdShot() {
  Gfx& g = os.gfx();
  const uint8_t* canvas = g.canvas();
  Serial.printf("SHOT %d %d\n", Gfx::W, Gfx::H);
  // base64 は 3 バイト単位なので、行の長さは 3 の倍数にする。
  constexpr size_t kRow = Gfx::W / 2;
  uint8_t row[kRow];
  unsigned char encoded[kRow / 3 * 4 + 8];
  for (int16_t y = 0; y < Gfx::H; ++y) {
    for (int16_t x = 0; x < Gfx::W; x += 2) {
      const uint8_t a = canvas[(Gfx::kPanelH - 1 - x) * Gfx::kPanelW + y];
      const uint8_t b = canvas[(Gfx::kPanelH - 2 - x) * Gfx::kPanelW + y];
      row[x / 2] = (a & 0xF0) | (b >> 4);
    }
    size_t out = 0;
    mbedtls_base64_encode(encoded, sizeof(encoded), &out, row, kRow);
    Serial.write(encoded, out);
    Serial.write('\n');
  }
  Serial.println("END");
}

void cmdKey(const char* arg) {
  Key k = Key::None;
  if (strcmp(arg, "a") == 0) k = Key::A;
  if (strcmp(arg, "b") == 0) k = Key::B;
  if (strcmp(arg, "A") == 0) k = Key::LongA;
  if (strcmp(arg, "B") == 0) k = Key::LongB;
  if (strcmp(arg, "ab") == 0) k = Key::Chord;
  if (k == Key::None) {
    Serial.println("ERR key a|b|A|B|ab");
    return;
  }
  os.injectKey(k);
  Serial.println("OK");
}

void cmdInfo() {
  const auto s = os.battery().readStatus();
  Serial.printf("app=%s wake=%s sd=%d heap=%u psram=%u bat=%umV pct=%u ext=%d chg=%d up=%lus lastUpdate=%lums\n",
                os.current() != nullptr ? os.current()->id() : "-", os.wakeReason(), os.sdReady(),
                ESP.getFreeHeap(), ESP.getFreePsram(), s.millivolts, s.percentage, s.externalPower, s.charging,
                millis() / 1000, os.gfx().lastUpdateMs());
  Serial.println("OK");
}

void cmdLs(const char* path) {
  auto& sd = SDCardManager::getInstance();
  FsFile dir = sd.open(path[0] != '\0' ? path : "/", O_RDONLY);
  if (!dir) {
    Serial.println("ERR open");
    return;
  }
  char name[128];
  FsFile entry;
  while (entry.openNext(&dir, O_RDONLY)) {
    entry.getName(name, sizeof(name));
    Serial.printf("%10llu %s%s\n", static_cast<unsigned long long>(entry.fileSize()), name,
                  entry.isDirectory() ? "/" : "");
    entry.close();
  }
  Serial.println("OK");
}

// put <パス> <大きさ> のあと、base64 の行を送ってもらう。
void cmdPut(char* arg) {
  char* space = strrchr(arg, ' ');
  if (space == nullptr) {
    Serial.println("ERR put path size");
    return;
  }
  *space = '\0';
  const size_t expected = strtoul(space + 1, nullptr, 10);
  auto& sd = SDCardManager::getInstance();
  char dir[192];
  snprintf(dir, sizeof(dir), "%s", arg);
  char* slash = strrchr(dir, '/');
  if (slash != nullptr && slash != dir) {
    *slash = '\0';
    sd.mkdir(dir, true);
  }
  FsFile f = sd.open(arg, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) {
    Serial.println("ERR open");
    return;
  }
  Serial.println("READY");
  size_t got = 0;
  char line[1024];
  uint8_t data[768];
  uint32_t last = millis();
  while (got < expected && millis() - last < 8000) {
    size_t n = 0;
    bool complete = false;
    while (millis() - last < 8000) {
      const int c = os.usbRead();
      if (c < 0) {
        delay(1);
        continue;
      }
      if (c == '\n') {
        complete = true;
        break;
      }
      if (n < sizeof(line)) line[n++] = static_cast<char>(c);
    }
    if (!complete) break;
    last = millis();
    size_t out = 0;
    if (mbedtls_base64_decode(data, sizeof(data), &out, reinterpret_cast<unsigned char*>(line), n) != 0) {
      Serial.println("ERR base64");
      break;
    }
    f.write(data, out);
    got += out;
    Serial.println("+");
  }
  f.close();
  Serial.println(got == expected ? "OK" : "ERR short");
}

// get <パス>: 大きさを送ってから base64 の行を送る。
void cmdGet(const char* path) {
  FsFile f = SDCardManager::getInstance().open(path, O_RDONLY);
  if (!f) {
    Serial.println("ERR open");
    return;
  }
  Serial.printf("SIZE %llu\n", static_cast<unsigned long long>(f.fileSize()));
  uint8_t data[720];
  unsigned char encoded[sizeof(data) / 3 * 4 + 8];
  int n;
  while ((n = f.read(data, sizeof(data))) > 0) {
    size_t out = 0;
    mbedtls_base64_encode(encoded, sizeof(encoded), &out, data, n);
    Serial.write(encoded, out);
    Serial.write('\n');
  }
  Serial.println("OK");
}

void execute(char* line) {
  char* arg = strchr(line, ' ');
  if (arg != nullptr) {
    *arg++ = '\0';
  } else {
    arg = line + strlen(line);
  }
  if (strcmp(line, "key") == 0) {
    cmdKey(arg);
  } else if (strcmp(line, "shot") == 0) {
    cmdShot();
  } else if (strcmp(line, "info") == 0) {
    cmdInfo();
  } else if (strcmp(line, "ls") == 0) {
    cmdLs(arg);
  } else if (strcmp(line, "put") == 0) {
    cmdPut(arg);
  } else if (strcmp(line, "get") == 0) {
    cmdGet(arg);
  } else if (strcmp(line, "rm") == 0) {
    Serial.println(SDCardManager::getInstance().remove(arg) ? "OK" : "ERR rm");
  } else if (strcmp(line, "app") == 0) {
    os.launch(arg);
    Serial.println("OK");
  } else if (strcmp(line, "sleep") == 0) {
    Serial.println("OK");
    Serial.flush();
    os.deepSleepNow(strtoul(arg, nullptr, 10));
  } else if (strcmp(line, "lsleep") == 0) {
    Serial.println("OK");
    Serial.flush();
    os.lightSleepFor(strtoul(arg, nullptr, 10));
  } else if (strcmp(line, "reboot") == 0) {
    Serial.println("OK");
    Serial.flush();
    ESP.restart();
  } else if (line[0] != '\0') {
    Serial.println("ERR unknown");
  }
}

}  // namespace

void poll() {
  // REPL へ入ると決まったら、あとの入力（mpremote の Ctrl-A など）は REPL のもの。
  if (os.replRequested()) return;
  int c;
  while ((c = os.usbPeek()) >= 0) {
    // mpremote や Thonny の呼びかけ。Ctrl-A は raw REPL への指示なので残しておく。
    if (c == 0x01 || c == 0x03) {
      if (c == 0x03) os.usbRead();
      gLen = 0;
      os.requestRepl();
      return;
    }
    os.usbRead();
    if (c == '\r') continue;
    if (c == '\n') {
      gLine[gLen] = '\0';
      gLen = 0;
      execute(gLine);
      continue;
    }
    if (gLen + 1 < sizeof(gLine)) gLine[gLen++] = static_cast<char>(c);
  }
}

}  // namespace console
}  // namespace pdos
