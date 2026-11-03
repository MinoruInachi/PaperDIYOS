#pragma once

// ネットから取ってきたものの読み解き。ハードに触らないのでホストで回せる。

#include <stddef.h>
#include <stdint.h>

namespace pdos {
namespace parse {

// --- 天気（Open-Meteo の JSON） ------------------------------------------------

struct Weather {
  float temperature = 0;
  float wind = 0;
  int humidity = 0;
  int code = 0;
  // 今日・明日・明後日
  float maxTemp[3] = {0, 0, 0};
  float minTemp[3] = {0, 0, 0};
  int dayCode[3] = {0, 0, 0};
  bool valid = false;
};

bool parseWeather(const char* json, Weather* out);
// WMO の天気コードを言葉にする。
const char* weatherText(int code, bool english = false);

// どの絵にするか。
enum class Sky : uint8_t { Sun, Cloud, Fog, Rain, Snow, Storm };
Sky weatherSky(int code);

// --- ニュース（RSS 2.0） -------------------------------------------------------

struct NewsItem {
  char title[160] = {0};
  char link[200] = {0};
  char description[280] = {0};
};

// xml の <item> を順に読む。読めた数を返す。xml は書き換えない。
uint16_t parseRss(const char* xml, NewsItem* items, uint16_t capacity);

// XML の実体参照を戻し、CDATA とタグを落として out へ詰める。
void cleanXmlText(const char* in, size_t len, char* out, size_t cap);

}  // namespace parse
}  // namespace pdos
