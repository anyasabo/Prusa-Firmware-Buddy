#!/usr/bin/env python3
"""Fails if a G-code handler reads a parameter value after parser.seen() with no value check.

parser.seen('X') is true for "M201 X" (a letter with no value). Then parser.value_float(),
value_long() and the other value_*() functions return 0, so "M201 X" sets a limit of 0 and
"M593 X F" sets an input shaper frequency of 0. Use parser.seenval('X'), or test
parser.has_value() before the read.

The check finds each "if (parser.seen('C'))" and the statement or the block that it controls, and
each "parser.seen('C') ? ... : ...". The
check fails if that code reads parser.value_*() (not value_bool(), which is true with no value)
before it tests parser.has_value() or calls parser.seen() again. A site where 0 is the correct
value for a letter with no value goes in ALLOWLIST, with the reason. A listed site that the check
no longer finds must leave the list, so the list stays current.

Usage: python3 utils/check_gcode_seen_value.py [REPO_ROOT]
"""

import pathlib
import re
import sys

SOURCE_DIRS = ("src", "lib/Marlin/Marlin/src")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}

# "path:letter": the reason why a letter with no value can give 0.
ALLOWLIST: dict[str, str] = {
    "lib/Marlin/Marlin/src/gcode/config/M200-M205.cpp:D":
    "M200 D with no value turns volumetric E off, as M200 D0 does",
    "lib/Marlin/Marlin/src/gcode/bedlevel/M420.cpp:C":
    "M420 C with no value moves the mesh to its mean with an offset of 0",
}

SEEN_IF = re.compile(r"\bif\s*\(\s*parser\.seen\(\s*'(\w)'\s*\)\s*\)")
# "parser.seen('V') ? parser.value_int() : 0"
SEEN_TERNARY = re.compile(r"\bparser\.seen\(\s*'(\w)'\s*\)\s*\?([^:;]*)")
READ = re.compile(r"\bparser\.value_(?!bool\b)\w+\s*\(")
STOP = re.compile(r"\bparser\.(?:has_value|seen|seenval|seen_test)\s*\(")


def controlled_code(text: str, start: int) -> str:
    """Returns the statement or the block that starts at start (after the if condition)."""
    i = start
    while i < len(text) and text[i].isspace():
        i += 1
    if i < len(text) and text[i] == "{":
        depth = 0
        for j in range(i, len(text)):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    return text[i:j + 1]
        return text[i:]
    end = text.find(";", i)
    return text[i:end + 1 if end >= 0 else len(text)]


def unchecked_reads(text: str) -> list[tuple[int, str]]:
    """Returns (line, letter) for each seen() whose code reads a value with no value check."""
    found = []
    for m in SEEN_IF.finditer(text):
        code = controlled_code(text, m.end())
        read = READ.search(code)
        if not read:
            continue
        stop = STOP.search(code)
        if stop and stop.start() < read.start():
            continue
        found.append((text.count("\n", 0, m.start()) + 1, m.group(1)))
    for m in SEEN_TERNARY.finditer(text):
        if READ.search(m.group(2)):
            found.append((text.count("\n", 0, m.start()) + 1, m.group(1)))
    return sorted(found)


def find_problems(root: pathlib.Path) -> tuple[list[str], list[str]]:
    """Returns (sites that read with no value check, allowlisted sites that the check does not find)."""
    problems = []
    allowed_used = set()
    for top in SOURCE_DIRS:
        for path in sorted((root / top).rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            text = path.read_text(encoding="utf-8", errors="replace")
            if "parser.seen(" not in text:
                continue
            for line, letter in unchecked_reads(text):
                key = f"{rel}:{letter}"
                if key in ALLOWLIST:
                    allowed_used.add(key)
                else:
                    problems.append(f"{rel}:{line}: {letter}")
    stale = sorted(set(ALLOWLIST) - allowed_used)
    return problems, stale


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    problems, stale = find_problems(root)
    for item in problems:
        print(
            f"{item}: parser.seen() is true for a letter with no value, and then value_*() "
            f"returns 0. Use parser.seenval()")
    for key in stale:
        print(
            f"{key}: the check does not find this site now: remove it from ALLOWLIST in {__file__}"
        )
    return 1 if problems or stale else 0


if __name__ == "__main__":
    sys.exit(main())
