#!/usr/bin/env python3
"""Fails if a G-code handler reads parser.string_arg with no null check.

The Marlin parser (lib/Marlin/Marlin/src/gcode/parser.cpp) sets string_arg to null if a command has
no text argument. Examples of the bug: "M340", "M997" and "M862.3 P1" read string_arg when it was
null.

The check splits each source file at the function definitions. A function that reads
parser.string_arg must also test it: "!parser.string_arg", "parser.string_arg &&",
"parser.string_arg ?", "... && parser.string_arg)", "if (parser.string_arg)" or a comparison with
nullptr. The rule is also for the handlers of the M codes for which the parser always sets
string_arg (for example M23), because another handler can call them (M32 calls M23).

The check finds a missing test, not a test in the wrong place: a test after the use also passes. A
function that tests string_arg in another way goes in ALLOWLIST, with the reason. A listed function
that no longer reads string_arg must leave the list, so the list stays current.

Usage: python3 utils/check_gcode_string_arg.py [REPO_ROOT]
"""

import pathlib
import re
import sys

SOURCE_DIRS = ("src", "lib/Marlin/Marlin/src")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}

# Function name: the reason why it needs no check that the script can see.
ALLOWLIST = {
    "M0_M1": "copies string_arg to args and tests 'args && *args'",
    "M16":
    "compiled only with EXPECTED_PRINTER_CHECK, which no printer enables",
}

USE = re.compile(r"\bparser\.string_arg\b")
CHECK = re.compile(
    r"!\s*parser\.string_arg\b"
    r"|\bparser\.string_arg\s*(?:&&|\?|[!=]=\s*(?:nullptr|NULL)\b)"
    r"|&&\s*parser\.string_arg\s*(?:\)|&&)"
    r"|\bif\s*\(\s*parser\.string_arg\s*\)")
# A function definition at the start of a line, for example "void GcodeSuite::M23() {",
# "static const char *f(int a) {" or "[[nodiscard]] bool f() {"
DEFINITION = re.compile(
    r"^[ \t]*(?:\[\[\w+\]\]\s*)?(?:(?:static|inline|constexpr|const)\s+)*[\w:<>]+[ \t*&]+(?:\w+::)*(\w+)"
    r"\s*\([^;{}]*\)\s*(?:const\s*)?\{", re.MULTILINE)
# Statements that DEFINITION also matches, for example "} else if (x) {" on its own line
KEYWORDS = {"if", "for", "while", "switch", "catch", "return"}


def functions(text: str) -> list[tuple[str, int, str]]:
    """Splits text at the function definitions. Returns (name, line, body) for each part."""
    matches = [
        m for m in DEFINITION.finditer(text) if m.group(1) not in KEYWORDS
    ]
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
    unchecked = []
    allowed_used = set()
    for top in SOURCE_DIRS:
        for path in sorted((root / top).rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            if rel.startswith("lib/Marlin/Marlin/src/gcode/parser."):
                continue  # the parser sets string_arg
            text = path.read_text(encoding="utf-8", errors="replace")
            if not USE.search(text):
                continue
            for name, line, body in functions(text):
                if not USE.search(body):
                    continue
                if name in ALLOWLIST:
                    allowed_used.add(name)
                elif not CHECK.search(body):
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
