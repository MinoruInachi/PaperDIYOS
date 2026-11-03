#pragma once

// アプリの小さな状態の保存先（Badger の badger_os.state_save / state_load）。
//
// Badger は JSON をフラッシュに置くが、こちらは固定長の構造体を NVS に置く。
// SD が無くても働き、deep sleep をまたいで残り、電源を切っても消えない。
// 大きなもの（読書位置やしおり）は SD に置く。
//
// 中身が同じなら書かないので、頻繁に呼んでも NVS を傷めない。

#include <stddef.h>
#include <stdint.h>

namespace pdos {
namespace store {

// 形が変わると size が合わなくなり、load は false を返す（既定値で始まる）。
bool loadBlob(const char* key, void* out, size_t size);
void saveBlob(const char* key, const void* data, size_t size);
void remove(const char* key);

template <typename T>
bool load(const char* key, T& out) {
  return loadBlob(key, &out, sizeof(T));
}

template <typename T>
void save(const char* key, const T& value) {
  saveBlob(key, &value, sizeof(T));
}

}  // namespace store
}  // namespace pdos
