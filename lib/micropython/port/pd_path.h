#pragma once

#include <stdbool.h>
#include <stddef.h>

// カレントディレクトリは Python のあいだだけ意味を持つので、ここで持つ。
void pd_path_reset(void);
const char *pd_path_cwd(void);
bool pd_path_chdir(const char *path);
// 相対パスを SD の絶対パスへ。. と .. も解く。
bool pd_path_resolve(const char *path, char *out, size_t cap);
