"""python/lib と python/examples を C の配列にして src/python/Embedded.h へ書く。

PlatformIO の pre スクリプトとして毎回走る。中身が同じなら書き直さない。
"""
import os

try:
    Import("env")  # noqa: F821
    ROOT = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def collect(directory):
    out = []
    base = os.path.join(ROOT, directory)
    for dirpath, _, names in os.walk(base):
        for name in sorted(names):
            if name.endswith(".py"):
                full = os.path.join(dirpath, name)
                out.append((os.path.relpath(full, base).replace(os.sep, "/"), open(full, "rb").read()))
    return sorted(out)


def table(symbol, items, lines):
    for i, (_, data) in enumerate(items):
        lines.append(f"inline constexpr char {symbol}Data{i}[] = {{")
        for j in range(0, len(data), 32):
            lines.append("  " + ",".join(str(b) for b in data[j:j + 32]) + ",")
        lines.append("  0};")
    lines.append(f"inline constexpr Entry {symbol}[] = {{")
    for i, (name, data) in enumerate(items):
        lines.append(f'  {{"{name}", {symbol}Data{i}, {len(data)}}},')
    lines.append("};")
    lines.append(f"inline constexpr size_t {symbol}Count = {len(items)};")
    lines.append("")


lines = [
    "#pragma once",
    "",
    "// tools/embed_py.py が生成。手で直さないこと。",
    "",
    "#include <stddef.h>",
    "",
    "namespace pdos {",
    "namespace python {",
    "",
    "struct Entry {",
    "  const char* name;",
    "  const char* data;",
    "  size_t size;",
    "};",
    "",
]
table("kLib", collect("python/lib"), lines)
table("kExamples", collect("python/examples"), lines)
lines += ["}  // namespace python", "}  // namespace pdos", ""]
text = "\n".join(lines)

path = os.path.join(ROOT, "src/python/Embedded.h")
old = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
if old != text:
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
