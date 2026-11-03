#include "I18n.h"

#include "Store.h"

namespace pdos {
namespace i18n {
namespace {

constexpr const char* kKey = "os.lang";
Lang gLang = Lang::Ja;

}  // namespace

Lang lang() { return gLang; }

void set(Lang l) {
  gLang = l;
  store::save(kKey, static_cast<uint8_t>(l));
}

void load() {
  uint8_t v = 0;
  if (store::load(kKey, v) && v <= 1) gLang = static_cast<Lang>(v);
}

}  // namespace i18n
}  // namespace pdos
