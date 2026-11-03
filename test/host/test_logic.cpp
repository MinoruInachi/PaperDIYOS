// ハードに触らない部分のテスト。test/host/run.sh で回す。

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "apps/Parse.h"
#include "os/Buttons.h"

using namespace pdos;

namespace {

int gChecks = 0;
#define CHECK(cond)                                         \
  do {                                                      \
    ++gChecks;                                              \
    if (!(cond)) {                                          \
      printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                             \
    }                                                       \
  } while (0)

// 5ms ごとに進めて、最初に出た操作を返す。
Key run(ButtonDecoder& d, bool a, bool b, uint32_t& now, uint32_t ms) {
  Key got = Key::None;
  for (uint32_t t = 0; t < ms; t += 5) {
    now += 5;
    const Key k = d.update(a, b, now);
    if (k != Key::None && got == Key::None) got = k;
  }
  return got;
}

int testClick() {
  ButtonDecoder d;
  uint32_t now = 0;
  CHECK(run(d, false, false, now, 50) == Key::None);
  CHECK(run(d, true, false, now, 100) == Key::None);  // 押しただけでは出ない
  CHECK(run(d, false, false, now, 50) == Key::A);     // 離して確定
  CHECK(run(d, false, true, now, 100) == Key::None);
  CHECK(run(d, false, false, now, 50) == Key::B);
  return 0;
}

int testLong() {
  ButtonDecoder d;
  uint32_t now = 0;
  CHECK(run(d, false, true, now, ButtonDecoder::kLongMs + 50) == Key::LongB);  // 離さなくても出る
  CHECK(run(d, false, true, now, 500) == Key::None);                           // 二度は出ない
  CHECK(run(d, false, false, now, 50) == Key::None);                           // 離しても単押しにならない
  CHECK(run(d, true, false, now, 100) == Key::None);
  CHECK(run(d, false, false, now, 50) == Key::A);  // 次の押下はふつうに効く
  return 0;
}

int testChord() {
  ButtonDecoder d;
  uint32_t now = 0;
  // A を少し先に押してから B。同時押しになり、単押しは出ない。
  CHECK(run(d, true, false, now, 60) == Key::None);
  CHECK(run(d, true, true, now, 60) == Key::Chord);
  CHECK(run(d, false, true, now, 60) == Key::None);
  CHECK(run(d, false, false, now, 60) == Key::None);
  CHECK(run(d, true, false, now, 60) == Key::None);
  CHECK(run(d, false, false, now, 60) == Key::A);
  return 0;
}

int testBounce() {
  ButtonDecoder d;
  uint32_t now = 0;
  // 10ms 未満のチャタリングは押下にならない。
  CHECK(run(d, true, false, now, 5) == Key::None);
  CHECK(run(d, false, false, now, 5) == Key::None);
  CHECK(run(d, true, false, now, 5) == Key::None);
  CHECK(run(d, false, false, now, 100) == Key::None);
  return 0;
}

int testSeed() {
  ButtonDecoder d;
  uint32_t now = 1000;
  // 起こしたボタンを押したまま起動: そのまま離せば単押し。
  d.seed(false, true, now);
  CHECK(run(d, false, false, now, 50) == Key::B);
  // 押し続ければ長押し。
  d.seed(true, false, now);
  CHECK(run(d, true, false, now, ButtonDecoder::kLongMs + 50) == Key::LongA);
  return 0;
}

int testWeather() {
  const char* json =
      R"({"latitude":35.7,"current_units":{"temperature_2m":"°C"},)"
      R"("current":{"time":"2026-10-31T18:00","temperature_2m":17.4,"relative_humidity_2m":62,)"
      R"("wind_speed_10m":3.2,"weather_code":61},)"
      R"("daily_units":{"temperature_2m_max":"°C"},)"
      R"("daily":{"time":["a","b","c"],"temperature_2m_max":[20.1,18,-1.5],"temperature_2m_min":[12,9.5,-3],)"
      R"("weather_code":[61,3,71]}})";
  parse::Weather w;
  CHECK(parse::parseWeather(json, &w));
  CHECK(w.valid);
  CHECK(w.temperature > 17.3f && w.temperature < 17.5f);
  CHECK(w.humidity == 62);
  CHECK(w.code == 61);
  CHECK(w.wind > 3.1f && w.wind < 3.3f);
  CHECK(w.maxTemp[2] < -1.4f && w.maxTemp[2] > -1.6f);
  CHECK(w.minTemp[1] > 9.4f && w.minTemp[1] < 9.6f);
  CHECK(w.dayCode[2] == 71);
  CHECK(strcmp(parse::weatherText(61), "雨") == 0);
  CHECK(parse::weatherSky(61) == parse::Sky::Rain);
  CHECK(parse::weatherSky(71) == parse::Sky::Snow);
  CHECK(parse::weatherSky(95) == parse::Sky::Storm);
  CHECK(parse::weatherSky(0) == parse::Sky::Sun);
  CHECK(!parse::parseWeather("{\"error\":true}", &w));
  return 0;
}

int testRss() {
  const char* xml =
      "<rss><channel><title>チャンネル名</title><link>http://x/</link>"
      "<item><title>猫 &amp; 犬</title><link>https://a.example/1</link>"
      "<description><![CDATA[<p>本文の<b>説明</b></p>]]></description></item>"
      "<item>\n <title type=\"text\"><![CDATA[二つ目]]></title>\n <link>https://a.example/2</link>\n</item>"
      "<item><link>https://a.example/notitle</link></item>"
      "</channel></rss>";
  parse::NewsItem items[4];
  const uint16_t n = parse::parseRss(xml, items, 4);
  CHECK(n == 2);
  CHECK(strcmp(items[0].title, "猫 & 犬") == 0);
  CHECK(strcmp(items[0].link, "https://a.example/1") == 0);
  CHECK(strcmp(items[0].description, "本文の説明") == 0);
  CHECK(strcmp(items[1].title, "二つ目") == 0);
  CHECK(items[1].description[0] == '\0');
  // 入り切らない分は切る。
  CHECK(parse::parseRss(xml, items, 1) == 1);
  // 切り詰めで UTF-8 の文字を割らない。
  char small[12];
  parse::cleanXmlText("あいうえおかきくけこ", strlen("あいうえおかきくけこ"), small, sizeof(small));
  CHECK(strlen(small) % 3 == 0);
  return 0;
}

}  // namespace

int main() {
  int failed = 0;
  failed += testClick();
  failed += testLong();
  failed += testChord();
  failed += testBounce();
  failed += testSeed();
  failed += testWeather();
  failed += testRss();
  printf("%d checks, %s\n", gChecks, failed == 0 ? "ok" : "FAILED");
  return failed;
}
