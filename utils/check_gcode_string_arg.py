#!/usr/bin/env python3
"""Fails if a G-code handler reads parser.string_arg with no null check.

The Marlin parser (lib/Marlin/Marlin/src/gcode/parser.cpp) sets string_arg to null if a command has
no text argument. Examples of the bug: "M340", "M997" and "M862.3 P1" read string_arg when it was
null.

The check splits each source file at the function definitions. A function that reads
parser.string_arg must also test it: "!parser.string_arg", "parser.string_arg &&",
"parser.string_arg ?", "... && parser.string_arg)", "if (parser.string_arg)" or a comparison with
nullptr. The parser always sets
string_arg for some M codes (the "case" list before "string_arg = p; return;" in parser.cpp). The
handlers of these codes (for example M23) need no check. The script reads that list from parser.cpp.
A function that tests string_arg in another way goes in ALLOWLIST, with the reason. A listed function
that no longer reads string_arg must leave the list, so the list stays current.

Usage: python3 utils/check_gcode_string_arg.py [REPO_ROOT]
"""

import pathlib
import re
import sys

PARSER = "lib/Marlin/Marlin/src/gcode/parser.cpp"
SOURCE_DIRS = ("src", "lib/Marlin/Marlin/src")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}

# Function name: the reason why it needs no check that the script can see.
ALLOWLIST = {
    "M0_M1": "copies string_arg to args and tests 'args && *args'",
}

USE = re.compile(r"\bparser\.string_arg\b")
CHECK = re.compile(
    r"!\s*parser\.string_arg\b"
    r"|\bparser\.string_arg\s*(?:&&|\?|[!=]=\s*(?:nullptr|NULL)\b)"
    r"|&&\s*parser\.string_arg\s*(?:\)|&&)"
    r"|\bif\s*\(\s*parser\.string_arg\s*\)")
# A function definition at the start of a line: "void GcodeSuite::M23() {" or "static void f(...) {"
DEFINITION = re.compile(
    r"^[ \t]*(?:static\s+|inline\s+)*[\w:<>]+[ \t]+(?:\w+::)*(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?\{",
    re.MULTILINE)


def always_set_codes(root: pathlib.Path) -> set[str]:
    """Returns the handler names (for example "M23") for which the parser always sets string_arg."""
    text = (root / PARSER).read_text(encoding="utf-8")
    end = text.find("string_arg = p; return;")
    start = text.rfind("switch (codenum)", 0, end)
    if end < 0 or start < 0:
        raise RuntimeError(
            f"{PARSER}: cannot find the M codes that always have a string_arg")
    return {f"M{n}" for n in re.findall(r"case\s+(\d+)\s*:", text[start:end])}


def functions(text: str) -> list[tuple[str, int, str]]:
    """Splits text at the function definitions. Returns (name, line, body) for each part."""
    matches = list(DEFINITION.finditer(text))
    parts = []
    if matches and matches[0].start() > 0:
        parts.append(("<file scope>", 1, text[:matches[0].start()]))
    if not matches:
        parts.append(("<file scope>", 1, text))
    for i, m in enumerate(matches):
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        parts.append((m.group(1), text.count("\n", 0, m.start()) + 1,
                      text[m.start():end]))
    return parts


def find_problems(root: pathlib.Path) -> tuple[list[str], list[str]]:
    """Returns (functions that read string_arg with no check, allowlisted functions that do not read it)."""
    exempt = always_set_codes(root)
    unchecked = []
    allowed_used = set()
    for top in SOURCE_DIRS:
        for path in sorted((root / top).rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            if rel.startswith("lib/Marlin/Marlin/src/gcode/parser."):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            if not USE.search(text):
                continue
            for name, line, body in functions(text):
                if not USE.search(body):
                    continue
                if name in ALLOWLIST:
                    allowed_used.add(name)
                elif name not in exempt and not CHECK.search(body):
                    unchecked.append(f"{rel}:{line}: {name}")
    stale = sorted(set(ALLOWLIST) - allowed_used)
    return unchecked, stale


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    unchecked, stale = find_problems(root)
    for item in unchecked:
        print(
            f"{item} reads parser.string_arg with no null check. The parser sets it to null "
            f"if the command has no text argument")
    for name in stale:
        print(
            f"{name} does not read parser.string_arg now: remove it from ALLOWLIST in {__file__}"
        )
    return 1 if unchecked or stale else 0


if __name__ == "__main__":
    sys.exit(main())
