#pragma once

// FreeInk SDK のいくつかのライブラリ（FrontlightManager など）は、
// ファームウェア側が Logging.h を用意している前提で LOG_INF を呼ぶ。
// esp_log ではなく Serial に出すのが SDK の意図（Arduino の sdkconfig は
// CONFIG_LOG_DEFAULT_LEVEL_ERROR なので ESP_LOGI は消える）。
//
//   LOG_INF("タグ", "書式 %d", 値);

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

inline void logPrintf(const char level, const char* tag, const char* fmt, ...) {
  char buf[224];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.printf("[%c][%s] %s\n", level, tag, buf);
}

#define LOG_INF(tag, ...) logPrintf('I', (tag), __VA_ARGS__)
#define LOG_WRN(tag, ...) logPrintf('W', (tag), __VA_ARGS__)
#define LOG_ERR(tag, ...) logPrintf('E', (tag), __VA_ARGS__)
#define LOG_DBG(tag, ...) logPrintf('D', (tag), __VA_ARGS__)
