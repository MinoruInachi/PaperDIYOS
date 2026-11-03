#include "FontStore.h"

#include <Arduino.h>
#include <esp_partition.h>
#include <string.h>

#include <SDCardManager.h>

#include "Config.h"

namespace paperdiy {
namespace {

// 1 フェイスあたりのグリフキャッシュ。和文は字種が多いので広めに取る。
constexpr size_t kGlyphArenaBytes = 192u * 1024;

// フラッシュに焼くときのヘッダ（tools/pack_fonts.py が付ける）。
struct PackHeader {
  char magic[8];  // "JAFONT02"
  uint32_t count;
  uint32_t reserved;
};
struct PackEntry {
  uint32_t offset;
  uint32_t length;
  uint8_t styleFlags;
  uint8_t pad[3];
};

uint32_t fnv1a(const void* data, size_t len, uint32_t seed = 0x811C9DC5u) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t h = seed;
  for (size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

bool looksLikeFont(const uint8_t* p) {
  if (p == nullptr) return false;
  const uint32_t tag = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) | p[3];
  return tag == 0x00010000u || tag == 0x74727565u /* true */ || tag == 0x4F54544Fu /* OTTO */;
}

}  // namespace

bool FontStore::addFace(const uint8_t* data, uint32_t len, uint8_t styleFlags) {
  if (faceCount_ >= kMaxFaces || data == nullptr || len < 1024) return false;
  if (!looksLikeFont(data)) return false;

  void* arena = heap_caps_malloc(kGlyphArenaBytes, MALLOC_CAP_SPIRAM);
  if (arena == nullptr) arena = malloc(kGlyphArenaBytes);
  if (arena == nullptr) return false;
  glyphArenas_[faceCount_].init(arena, kGlyphArenaBytes);

  if (!faces_[faceCount_].init(data, len, glyphArenas_[faceCount_])) {
    free(arena);
    return false;
  }
  if (!chain_.add(&faces_[faceCount_], styleFlags)) {
    free(arena);
    return false;
  }
  // 指紋は「長さ＋先頭 256 バイト」で十分に区別できる。
  fingerprint_ = fnv1a(data, 256, fnv1a(&len, sizeof(len), fingerprint_));
  totalBytes_ += len;
  ++faceCount_;
  return true;
}

bool FontStore::loadFromFlash() {
  const esp_partition_t* part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(kFontPartitionSubtype),
      kFontPartitionName);
  if (part == nullptr) return false;

  const void* mapped = nullptr;
  esp_partition_mmap_handle_t handle = 0;
  if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &mapped, &handle) != ESP_OK) {
    return false;
  }
  const uint8_t* base = static_cast<const uint8_t*>(mapped);

  // 素の TTF がそのまま焼かれている場合も受け付ける。
  if (looksLikeFont(base)) {
    if (addFace(base, part->size, 0)) {
      source_ = "flash";
      return true;
    }
    esp_partition_munmap(handle);
    return false;
  }

  const PackHeader* header = reinterpret_cast<const PackHeader*>(base);
  if (memcmp(header->magic, "JAFONT02", 8) != 0 || header->count == 0 ||
      header->count > kMaxFaces) {
    esp_partition_munmap(handle);
    return false;
  }
  const PackEntry* entries = reinterpret_cast<const PackEntry*>(base + sizeof(PackHeader));
  bool any = false;
  for (uint32_t i = 0; i < header->count; ++i) {
    if (entries[i].offset + entries[i].length > part->size) continue;
    any |= addFace(base + entries[i].offset, entries[i].length, entries[i].styleFlags);
  }
  if (!any) {
    esp_partition_munmap(handle);
    return false;
  }
  source_ = "flash";
  // マップは電源が入っている間ずっと使うので解除しない。
  return true;
}

bool FontStore::loadFromSd() {
  SDCardManager& sd = SDCardManager::getInstance();
  if (!sd.ready()) return false;

  FsFile dir = sd.open(kFontDir, O_RDONLY);
  if (!dir) return false;

  FsFile entry;
  char name[128];
  while (entry.openNext(&dir, O_RDONLY) && faceCount_ < kMaxFaces) {
    if (entry.isDir() || !entry.getName(name, sizeof(name))) {
      entry.close();
      continue;
    }
    const size_t len = strlen(name);
    const bool isTtf = len > 4 && (strcasecmp(name + len - 4, ".ttf") == 0 ||
                                   strcasecmp(name + len - 4, ".otf") == 0);
    if (!isTtf) {
      entry.close();
      continue;
    }
    const uint32_t size = entry.size();
    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
    if (buf == nullptr) {
      entry.close();
      continue;
    }
    const int got = entry.read(buf, size);
    entry.close();
    if (got != static_cast<int>(size)) {
      free(buf);
      continue;
    }
    // 名前に bold が入っていれば太字フェイスとして登録する。
    uint8_t style = 0;
    for (size_t i = 0; i + 4 <= len; ++i) {
      if (strncasecmp(name + i, "bold", 4) == 0) {
        style = freeink::font::StyleBold;
        break;
      }
    }
    if (!addFace(buf, size, style)) {
      free(buf);
    } else {
      source_ = "sd";
      Serial.printf("[font] SD から読み込み: %s (%u KB, style %u)\n", name, size / 1024, style);
    }
  }
  dir.close();
  return faceCount_ > 0;
}

bool FontStore::begin() {
  if (loadFromFlash()) {
    Serial.printf("[font] フラッシュの font パーティションから %u フェイス (%u KB)\n", faceCount_,
                  totalBytes_ / 1024);
    return true;
  }
  if (loadFromSd()) return true;
  Serial.println("[font] 日本語フォントが見つかりません（flash/font または SD の /fonts）");
  return false;
}

}  // namespace paperdiy
