#!/usr/bin/env python3
"""Make a clang-tidy compile database from an ARM GCC firmware database.

clang-tidy has no --query-driver (clangd has), so this adds the target, -nostdinc and the system
include paths of the GCC in the database, and drops the GCC options that clang refuses. It also copies
the type and integer macros of the GCC (__UINT32_TYPE__, __UINT32_MAX__, __UINT32_C and others): for arm-none-eabi, GCC makes uint32_t an
unsigned long and clang an unsigned int, and then code such as std::max(uint32_t, 1ul) does not compile.
Usage: clang_tidy_db.py IN_DIR OUT_DIR
"""
import json, pathlib, re, shlex, subprocess, sys

src, dst = map(pathlib.Path, sys.argv[1:3])
entries = json.loads((src / "compile_commands.json").read_text())
DROP = {
    "-fno-extended-identifiers", "-fno-fat-lto-objects", "--specs=nosys.specs",
    "--specs=nano.specs", "-fpch-preprocess"
}
DROP_PREFIX = ("-flto", "-Wstack-usage", "-Wformat-overflow",
               "-Wformat-truncation", "-mlibarch", "-march=")
cache = {}


def system_includes(compiler, flags, lang):
    key = (compiler, tuple(flags), lang)
    if key not in cache:
        r = subprocess.run([
            compiler, *flags, "--specs=nano.specs", "-x" + lang, "-E", "-v",
            "-"
        ],
                           input="",
                           capture_output=True,
                           text=True)
        m = re.search(
            r"#include <\.\.\.> search starts here:\n(.*?)End of search list",
            r.stderr, re.S)
        cache[key] = [
            str(pathlib.Path(p.strip()).resolve())
            for p in m.group(1).splitlines()
        ] if m else []
    return cache[key]


type_cache = {}


def type_macros(compiler, flags):
    key = (compiler, tuple(flags))
    if key not in type_cache:
        r = subprocess.run([compiler, *flags, "-dM", "-E", "-x", "c", "-"],
                           input="",
                           capture_output=True,
                           text=True)
        # The integer macros too: newlib defines UINT32_MAX and UINT32_C() with __UINT32_MAX__ and
        # __UINT32_C(c), and their suffix must agree with the type.
        macros = re.findall(
            r"^#define ((__\w+_TYPE__|__U?INT\w*)(\(\w+\))?) (.+)$", r.stdout,
            re.M)
        type_cache[key] = [
            x for full, name, _, value in macros
            for x in ("-U" + name, f"-D{full}={value}")
        ]
    return type_cache[key]


out = []
for e in entries:
    args = e.get("arguments") or shlex.split(e["command"])
    compiler, rest = args[0], args[1:]
    cxx = compiler.endswith("g++")
    cpu = [
        a for a in rest
        if a.startswith(("-mcpu=", "-mfpu=", "-mfloat-abi=", "-mthumb"))
    ]
    inc = system_includes(compiler, cpu + (["-std=gnu++23"] if cxx else []),
                          "c++" if cxx else "c")
    rest = [a for a in rest if a not in DROP and not a.startswith(DROP_PREFIX)]
    sysargs = [x for p in inc for x in ("-isystem", p)]
    e2 = {k: v for k, v in e.items() if k != "command"}
    e2["arguments"] = [
        "clang++" if cxx else "clang", "--target=arm-none-eabi", "-nostdinc",
        "-Wno-unknown-warning-option", "-Wno-unused-command-line-argument",
        "-Wno-macro-redefined", *type_macros(compiler, cpu), *sysargs, *rest
    ]
    out.append(e2)
dst.mkdir(parents=True, exist_ok=True)
(dst / "compile_commands.json").write_text(json.dumps(out))
print(len(out), "entries,", len(cache), "include sets")
