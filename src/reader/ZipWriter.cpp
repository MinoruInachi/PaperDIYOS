#include "ZipWriter.h"

#include <stdio.h>
#include <string.h>

namespace paperdiy {
namespace {

constexpr uint32_t kLocalSig = 0x04034B50u;
constexpr uint32_t kCentralSig = 0x02014B50u;
constexpr uint32_t kEndSig = 0x06054B50u;
constexpr uint16_t kStored = 0;  // 無圧縮
constexpr uint16_t kVersion = 20;

}  // namespace

uint32_t zipCrc32(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

void ZipWriter::begin(ZipSinkFn sink, void* user) {
  sink_ = sink;
  user_ = user;
  count_ = 0;
  offset_ = 0;
  inEntry_ = false;
  failed_ = false;
}

bool ZipWriter::write(const void* data, size_t len) {
  if (failed_ || sink_ == nullptr) return false;
  if (!sink_(user_, data, len)) {
    failed_ = true;
    return false;
  }
  offset_ += static_cast<uint32_t>(len);
  return true;
}

bool ZipWriter::writeU16(uint16_t v) {
  const uint8_t buf[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
  return write(buf, sizeof(buf));
}

bool ZipWriter::writeU32(uint32_t v) {
  const uint8_t buf[4] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
                          static_cast<uint8_t>((v >> 16) & 0xFF),
                          static_cast<uint8_t>((v >> 24) & 0xFF)};
  return write(buf, sizeof(buf));
}

bool ZipWriter::beginEntry(const char* name, uint32_t size, uint32_t crc) {
  if (failed_ || inEntry_ || name == nullptr) return false;
  if (count_ >= kMaxZipEntries) {
    // ここで止まると、途中まで書いた ZIP は使い物にならない。
    // 何件目で溢れたかは呼び出し側（Aozora.cpp）が記録する。
    failed_ = true;
    return false;
  }

  Entry& entry = entries_[count_];
  snprintf(entry.name, sizeof(entry.name), "%s", name);
  entry.crc = crc;
  entry.size = size;
  entry.offset = offset_;
  written_ = 0;

  // 無圧縮なので、圧縮後の大きさは元の大きさと同じ。
  const uint16_t nameLen = static_cast<uint16_t>(strlen(entry.name));
  if (!writeU32(kLocalSig) || !writeU16(kVersion) || !writeU16(0) || !writeU16(kStored) ||
      !writeU16(0) || !writeU16(0) ||  // 時刻・日付（持たない）
      !writeU32(crc) || !writeU32(size) || !writeU32(size) || !writeU16(nameLen) || !writeU16(0)) {
    return false;
  }
  if (!write(entry.name, nameLen)) return false;

  inEntry_ = true;
  return true;
}

bool ZipWriter::addData(const void* data, size_t len) {
  if (failed_ || !inEntry_) return false;
  // 先に書いた大きさを超えると、ヘッダと中身が食い違う。
  if (written_ + len > entries_[count_].size) {
    failed_ = true;
    return false;
  }
  written_ += static_cast<uint32_t>(len);
  return write(data, len);
}

bool ZipWriter::endEntry() {
  if (failed_ || !inEntry_) return false;
  if (written_ != entries_[count_].size) {  // 足りないまま閉じた
    failed_ = true;
    return false;
  }
  inEntry_ = false;
  ++count_;
  return true;
}

bool ZipWriter::addFile(const char* name, const void* data, size_t len) {
  const uint32_t crc = zipCrc32(0, static_cast<const uint8_t*>(data), len);
  if (!beginEntry(name, static_cast<uint32_t>(len), crc)) return false;
  if (len > 0 && !addData(data, len)) return false;
  return endEntry();
}

bool ZipWriter::finish() {
  if (failed_ || inEntry_) return false;

  const uint32_t centralStart = offset_;
  for (uint8_t i = 0; i < count_; ++i) {
    const Entry& entry = entries_[i];
    const uint16_t nameLen = static_cast<uint16_t>(strlen(entry.name));
    if (!writeU32(kCentralSig) || !writeU16(kVersion) || !writeU16(kVersion) || !writeU16(0) ||
        !writeU16(kStored) || !writeU16(0) || !writeU16(0) || !writeU32(entry.crc) ||
        !writeU32(entry.size) || !writeU32(entry.size) || !writeU16(nameLen) || !writeU16(0) ||
        !writeU16(0) || !writeU16(0) || !writeU16(0) || !writeU32(0) || !writeU32(entry.offset)) {
      return false;
    }
    if (!write(entry.name, nameLen)) return false;
  }

  const uint32_t centralSize = offset_ - centralStart;
  if (!writeU32(kEndSig) || !writeU16(0) || !writeU16(0) || !writeU16(count_) ||
      !writeU16(count_) || !writeU32(centralSize) || !writeU32(centralStart) || !writeU16(0)) {
    return false;
  }
  return !failed_;
}

}  // namespace paperdiy
