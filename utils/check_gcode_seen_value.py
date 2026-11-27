#!/usr/bin/env python3
"""Fails if a G-code handler reads a parameter value after parser.seen() with no value check.

parser.seen('X') is true for "M201 X" (a letter with no value). Then parser.value_float(),
value_long() and the other value_*() functions return 0, so "M572 S" sets a pressure advance of 0
and "M302 S" allows cold extrusion. Use parser.seenval('X'), or test parser.has_value() before the
read.

The check finds these forms, and the code that each one controls:

- "if (parser.seen(C))": the statement or the block after it
- "parser.seen(C) ? A : B": A
- "bool name = parser.seen(C)", then "if (name)" or "name ? A : B"
- "if (!parser.seen(C)) return;": the rest of the block

The check fails if that code reads parser.value_*() (not value_bool(), which is true with no value)
before it calls parser.has_value() or parser.seenval(). A site where a letter with no value is
valid goes in ALLOWLIST as "function:letter", with the reason. A listed site that the check no
longer finds must leave the list, so the list stays current.

Usage: python3 utils/check_gcode_seen_value.py [REPO_ROOT]
"""

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from check_gcode_string_arg import functions  # noqa: E402

SOURCE_DIRS = ("src", "lib/Marlin/Marlin/src")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}

# "function:letter": the reason why a letter with no value is valid there.
ALLOWLIST = {
    "M200:'D'":
    "M200 D with no value turns volumetric E off, as M200 D0 does",
    "M420:'C'":
    "the doc: C with no value moves the mesh to its mean with no offset",
    "M420:'Z'":
    "the doc: Z with no value turns the fade off, as Z0 does",
    "M914:axis_codes[i]":
    "each axis case tests has_value() and then uses the default sensitivity",
}

SEEN = r"\bparser\.seen\(\s*([^()]+?)\s*\)"
SEEN_IF = re.compile(r"\bif\s*\(\s*" + SEEN + r"\s*\)")
# The true branch ends at the first ':' that is not part of '::'.
TERNARY_BRANCH = r"\s*\?((?:[^:;]|::)*)"
SEEN_TERNARY = re.compile(SEEN + TERNARY_BRANCH)
SEEN_BOOL = re.compile(r"\b(\w+)\s*=\s*" + SEEN + r"\s*[;,]")
SEEN_RETURN = re.compile(r"\bif\s*\(\s*!\s*" + SEEN +
                         r"\s*\)\s*return\b[^;]*;")
READ = re.compile(r"\bparser\.value_(?!bool\b)\w+\s*\(")
GUARD = re.compile(r"\bparser\.(?:has_value|seenval)\s*\(")


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


def rest_of_block(text: str, start: int) -> str:
    """Returns the text from start to the end of the block that holds start."""
    depth = 0
    for j in range(start, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth < 0:
                return text[start:j]
    return text[start:]


def unguarded_read(code: str) -> bool:
    """True if code reads a value before it tests has_value() or calls seenval()."""
    read = READ.search(code)
    if not read:
        return False
    guard = GUARD.search(code)
    return not guard or guard.start() > read.start()


def unchecked_reads(text: str) -> list[tuple[int, str]]:
    """Returns (line, letter) for each seen() whose code reads a value with no value check."""
    found = set()

    def line(pos: int) -> int:
        return text.count("\n", 0, pos) + 1

    for m in SEEN_IF.finditer(text):
        if unguarded_read(controlled_code(text, m.end())):
            found.add((line(m.start()), m.group(1)))
    for m in SEEN_TERNARY.finditer(text):
        if unguarded_read(m.group(2)):
            found.add((line(m.start()), m.group(1)))
    for m in SEEN_RETURN.finditer(text):
        if unguarded_read(rest_of_block(text, m.end())):
            found.add((line(m.start()), m.group(1)))
    for m in SEEN_BOOL.finditer(text):
        name = re.escape(m.group(1))
        rest = rest_of_block(text, m.end())
        for use in re.finditer(r"\bif\s*\(\s*" + name + r"\s*\)", rest):
            if unguarded_read(controlled_code(rest, use.end())):
                found.add((line(m.start()), m.group(2)))
        for use in re.finditer(r"\b" + name + TERNARY_BRANCH, rest):
            if unguarded_read(use.group(1)):
                found.add((line(m.start()), m.group(2)))
    return sorted(found)


def find_problems(
        root: pathlib.Path,
        allowlist: dict[str, str] = ALLOWLIST) -> tuple[list[str], list[str]]:
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
            for name, start, body in functions(text):
                for line, letter in unchecked_reads(body):
                    key = f"{name}:{letter}"
                    if key in allowlist:
                        allowed_used.add(key)
                    else:
                        problems.append(
                            f"{rel}:{start + line - 1}: {name}: {letter}")
    stale = sorted(set(allowlist) - allowed_used)
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
