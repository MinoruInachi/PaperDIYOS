#include "Store.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace pdos {
namespace store {
namespace {

constexpr const char* kNamespace = "paperdiy";

}  // namespace

bool loadBlob(const char* key, void* out, size_t size) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;
  const bool ok = prefs.isKey(key) && prefs.getBytesLength(key) == size && prefs.getBytes(key, out, size) == size;
  prefs.end();
  return ok;
}

void saveBlob(const char* key, const void* data, size_t size) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  bool same = false;
  if (prefs.isKey(key) && prefs.getBytesLength(key) == size) {
    void* old = heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
    if (old != nullptr) {
      same = prefs.getBytes(key, old, size) == size && memcmp(old, data, size) == 0;
      heap_caps_free(old);
    }
  }
  if (!same) prefs.putBytes(key, data, size);
  prefs.end();
}

void remove(const char* key) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.remove(key);
  prefs.end();
}

}  // namespace store
}  // namespace pdos
