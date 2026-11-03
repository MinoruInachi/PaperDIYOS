#include "Sample.h"

#include <SDCardManager.h>
#include <stdio.h>

#include "TextFile.h"
#include "python/Embedded.h"

namespace pdos {
namespace sample {
namespace {

// 夏目漱石『吾輩は猫である』冒頭（著作権の保護期間は満了）。青空文庫形式のルビつき。
constexpr const char* kBook =
    "吾輩は猫である\n"
    "夏目漱石\n"
    "\n"
    "　吾輩《わがはい》は猫である。名前はまだ無い。\n"
    "　どこで生れたかとんと見当《けんとう》がつかぬ。何でも薄暗いじめじめした所で"
    "ニャーニャー泣いていた事だけは記憶している。吾輩はここで始めて人間というものを見た。"
    "しかもあとで聞くとそれは書生という人間中で一番｜獰悪《どうあく》な種族であったそうだ。"
    "この書生というのは時々我々を捕《つかま》えて煮《に》て食うという話である。\n";

constexpr const char* kBadge =
    "見本株式会社\n"
    "山田 太郎\n"
    "担当\n"
    "電子ペーパー\n"
    "端末\n"
    "PaperDIY 960x540\n"
    "/badges/badge.jpg";

constexpr const char* kList =
    "フォントを焼く\n"
    "SD に本を入れる\n"
    "縦組みで読む\n"
    "横組みに切り替える\n"
    "Wi-Fi の設定を書く\n"
    "時計を合わせる\n"
    "名札を自分のものにする\n";

constexpr const char* kQr =
    "https://github.com/Free-Ink/freeink-sdk\n"
    "FreeInk SDK\n"
    "この環境の表示を支えている\n"
    "電子ペーパー向けの SDK です。\n";

constexpr const char* kWifi =
    "# Wi-Fi はふつう「設定」アプリで選びます。ここに書くと、設定アプリで\n"
    "# 何も選んでいないときの接続先になります。\n"
    "# ssid = 家のWi-Fi\n"
    "# psk  = パスワード\n";

constexpr const char* kLocation =
    "# 緯度, 経度, 地名（天気アプリが使います）\n"
    "35.68, 139.76, 東京\n";

constexpr const char* kFeeds =
    "# ニュースのフィード。1 行に「名前, URL」\n"
    "NHK 主要, https://www3.nhk.or.jp/rss/news/cat0.xml\n"
    "ITmedia, https://rss.itmedia.co.jp/rss/2.0/itmedia_all.xml\n"
    "BBC Tech, https://feeds.bbci.co.uk/news/technology/rss.xml\n";

}  // namespace

void write() {
  auto& sd = SDCardManager::getInstance();
  for (const char* dir : {"/books", "/images", "/badges", "/lists", "/qrcodes", "/config", "/examples", "/lib", "/state"}) {
    sd.mkdir(dir, true);
  }
  textfile::writeIfMissing("/books/吾輩は猫である.txt", kBook);
  textfile::writeIfMissing("/badges/badge.txt", kBadge);
  textfile::writeIfMissing("/lists/checklist.txt", kList);
  textfile::writeIfMissing("/qrcodes/freeink.txt", kQr);
  textfile::writeIfMissing("/config/wifi.txt", kWifi);
  textfile::writeIfMissing("/config/location.txt", kLocation);
  textfile::writeIfMissing("/config/news.txt", kFeeds);
  for (size_t i = 0; i < python::kExamplesCount; ++i) {
    char path[64];
    snprintf(path, sizeof(path), "/examples/%s", python::kExamples[i].name);
    textfile::writeIfMissing(path, python::kExamples[i].data);
  }
}

}  // namespace sample
}  // namespace pdos
