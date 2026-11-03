#include "Parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace pdos {
namespace parse {
namespace {

// "key": の直後を返す。無ければ nullptr。
const char* afterKey(const char* json, const char* key) {
  char pattern[48];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char* p = strstr(json, pattern);
  return p != nullptr ? p + strlen(pattern) : nullptr;
}

bool readNumber(const char* section, const char* key, float* out) {
  const char* p = afterKey(section, key);
  if (p == nullptr) return false;
  char* end = nullptr;
  const float v = strtof(p, &end);
  if (end == p) return false;
  *out = v;
  return true;
}

// "key":[a,b,c] の先頭から count 個。
bool readArray(const char* section, const char* key, float* out, int count) {
  const char* p = afterKey(section, key);
  if (p == nullptr || *p != '[') return false;
  ++p;
  for (int i = 0; i < count; ++i) {
    char* end = nullptr;
    const float v = strtof(p, &end);
    if (end == p) return false;
    out[i] = v;
    p = end;
    if (*p == ',') ++p;
  }
  return true;
}

// 開始タグと閉じタグのあいだを探す。<title> も <title type="x"> も受ける。
bool findElement(const char* begin, const char* end, const char* tag, const char** bodyOut, size_t* lenOut) {
  char open[24];
  snprintf(open, sizeof(open), "<%s", tag);
  const size_t openLen = strlen(open);
  const char* p = begin;
  for (;;) {
    p = strstr(p, open);
    if (p == nullptr || p >= end) return false;
    const char next = p[openLen];
    if (next == '>' || next == ' ') break;
    p += openLen;
  }
  const char* body = strchr(p, '>');
  if (body == nullptr || body >= end) return false;
  ++body;
  char close[24];
  snprintf(close, sizeof(close), "</%s>", tag);
  const char* stop = strstr(body, close);
  if (stop == nullptr || stop > end) return false;
  *bodyOut = body;
  *lenOut = static_cast<size_t>(stop - body);
  return true;
}

}  // namespace

bool parseWeather(const char* json, Weather* out) {
  *out = Weather{};
  const char* current = strstr(json, "\"current\":{");
  const char* daily = strstr(json, "\"daily\":{");
  if (current == nullptr || daily == nullptr) return false;
  float humidity = 0, code = 0;
  if (!readNumber(current, "temperature_2m", &out->temperature)) return false;
  if (!readNumber(current, "wind_speed_10m", &out->wind)) return false;
  if (!readNumber(current, "relative_humidity_2m", &humidity)) return false;
  if (!readNumber(current, "weather_code", &code)) return false;
  out->humidity = static_cast<int>(humidity);
  out->code = static_cast<int>(code);
  float codes[3];
  if (!readArray(daily, "temperature_2m_max", out->maxTemp, 3)) return false;
  if (!readArray(daily, "temperature_2m_min", out->minTemp, 3)) return false;
  if (!readArray(daily, "weather_code", codes, 3)) return false;
  for (int i = 0; i < 3; ++i) out->dayCode[i] = static_cast<int>(codes[i]);
  out->valid = true;
  return true;
}

const char* weatherText(int code, bool english) {
  const auto pick = [english](const char* ja, const char* en) { return english ? en : ja; };
  if (code == 0) return pick("快晴", "Clear");
  if (code == 1) return pick("晴れ", "Mostly clear");
  if (code == 2) return pick("晴れ時々くもり", "Partly cloudy");
  if (code == 3) return pick("くもり", "Overcast");
  if (code == 45 || code == 48) return pick("霧", "Fog");
  if (code >= 51 && code <= 57) return pick("霧雨", "Drizzle");
  if (code >= 61 && code <= 67) return pick("雨", "Rain");
  if (code >= 71 && code <= 77) return pick("雪", "Snow");
  if (code >= 80 && code <= 82) return pick("にわか雨", "Showers");
  if (code == 85 || code == 86) return pick("にわか雪", "Snow showers");
  if (code >= 95) return pick("雷雨", "Thunderstorm");
  return pick("不明", "Unknown");
}

Sky weatherSky(int code) {
  if (code <= 1) return Sky::Sun;
  if (code <= 3) return Sky::Cloud;
  if (code == 45 || code == 48) return Sky::Fog;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if (code >= 95) return Sky::Storm;
  return Sky::Rain;
}

void cleanXmlText(const char* in, size_t len, char* out, size_t cap) {
  size_t o = 0;
  size_t i = 0;
  // CDATA は中身だけ。
  if (len >= 12 && strncmp(in, "<![CDATA[", 9) == 0) {
    const char* stop = strstr(in + 9, "]]>");
    if (stop != nullptr && static_cast<size_t>(stop - in) <= len) {
      i = 9;
      len = static_cast<size_t>(stop - in);
    }
  }
  bool inTag = false;
  bool pendingSpace = false;
  while (i < len && o + 5 < cap) {
    const char c = in[i];
    if (inTag) {
      if (c == '>') inTag = false;
      ++i;
      continue;
    }
    if (c == '<') {
      inTag = true;
      ++i;
      continue;
    }
    // 改行と連続した空白は 1 つにまとめる。
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      pendingSpace = o > 0;
      ++i;
      continue;
    }
    if (pendingSpace) {
      out[o++] = ' ';
      pendingSpace = false;
    }
    if (c == '&') {
      static const struct {
        const char* name;
        char value;
      } kEntities[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&#39;", '\''}, {"&nbsp;", ' '}};
      bool matched = false;
      for (const auto& e : kEntities) {
        const size_t n = strlen(e.name);
        if (i + n <= len && strncmp(in + i, e.name, n) == 0) {
          out[o++] = e.value;
          i += n;
          matched = true;
          break;
        }
      }
      if (matched) continue;
    }
    out[o++] = c;
    ++i;
  }
  // 切り詰めで UTF-8 の途中にならないように。
  if (i < len) {
    while (o > 0 && (static_cast<unsigned char>(out[o - 1]) & 0xC0) == 0x80) --o;
    if (o > 0 && static_cast<unsigned char>(out[o - 1]) >= 0xC0) --o;
  }
  out[o] = '\0';
}

uint16_t parseRss(const char* xml, NewsItem* items, uint16_t capacity) {
  uint16_t count = 0;
  const char* p = xml;
  while (count < capacity) {
    const char* begin = strstr(p, "<item");
    if (begin == nullptr) break;
    const char* end = strstr(begin, "</item>");
    if (end == nullptr) break;
    NewsItem& item = items[count];
    item = NewsItem{};
    const char* body;
    size_t len;
    if (findElement(begin, end, "title", &body, &len)) cleanXmlText(body, len, item.title, sizeof(item.title));
    if (findElement(begin, end, "link", &body, &len)) cleanXmlText(body, len, item.link, sizeof(item.link));
    if (findElement(begin, end, "description", &body, &len)) {
      cleanXmlText(body, len, item.description, sizeof(item.description));
    }
    if (item.title[0] != '\0') ++count;
    p = end + 7;
  }
  return count;
}

}  // namespace parse
}  // namespace pdos
