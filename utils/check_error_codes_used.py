#!/usr/bin/env python3
"""Fails if an error code of buddy-error-codes.yaml has no use in the firmware source.

An error code that nothing raises is often a missing check. Example: LOADCELL_TIMEOUT (XX528) was
defined, but no code raised it, and a disconnected load cell gave no error during a print.

The identifiers are made as lib/Prusa-Error-Codes/generate_buddy_headers.py makes them:
<class>_<id>, where the class is the third digit of the code. The check skips deprecated codes and
the BOOTLOADER class (the closed bootloader raises those by number). A code that has no use and
must stay goes in utils/error_codes_unused_allowlist.txt, with the reason as a comment. A listed
code that gets a use must leave the list, so the list stays current.

The yaml is read with a line parser, so the pre-commit hook needs no PyYAML.

Usage: python3 utils/check_error_codes_used.py [REPO_ROOT]
"""

import pathlib
import re
import sys

YAML = "lib/Prusa-Error-Codes/yaml/buddy-error-codes.yaml"
ALLOWLIST = "utils/error_codes_unused_allowlist.txt"
SOURCE_DIRS = ("src", "include", "lib/Marlin")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".inc", ".ipp"}

# The same as err_class_mapping in generate_buddy_headers.py. Class 6 (BOOTLOADER) is skipped.
CLASSES = {
    1: "ERR_MECHANICAL",
    2: "ERR_TEMPERATURE",
    3: "ERR_ELECTRO",
    4: "ERR_CONNECT",
    5: "ERR_SYSTEM",
    7: "WARNING",
    8: "CONNECT",
    9: "ERR_OTHER",
}


def read_codes(root: pathlib.Path) -> dict[str, str]:
    """Returns {identifier: code} for each error that is not deprecated."""
    codes: dict[str, str] = {}
    entry: dict[str, str] = {}

    def finish() -> None:
        if "code" in entry and "id" in entry and entry.get(
                "deprecated") != "true":
            err_class = int(entry["code"][2])
            if err_class in CLASSES:
                codes[f"{CLASSES[err_class]}_{entry['id']}"] = entry["code"]

    for line in (root / YAML).read_text(encoding="utf-8").splitlines():
        if m := re.match(r'^\s*- code:\s*"?([^"\s]+)"?', line):
            finish()
            entry = {"code": m.group(1)}
        elif m := re.match(r'^\s+(id|deprecated):\s*"?([^"\s]+)"?', line):
            entry[m.group(1)] = m.group(2)
    finish()
    return codes


def read_allowlist(root: pathlib.Path) -> set[str]:
    path = root / ALLOWLIST
    if not path.exists():
        return set()
    names = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        name = line.split("#", 1)[0].strip()
        if name:
            names.add(name)
    return names


def used_identifiers(root: pathlib.Path, wanted: set[str]) -> set[str]:
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, wanted)) + r")\b")
    used: set[str] = set()
    for top in SOURCE_DIRS:
        for path in (root / top).rglob("*"):
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                used.update(
                    pattern.findall(
                        path.read_text(encoding="utf-8", errors="replace")))
    return used


def find_problems(root: pathlib.Path) -> tuple[list[str], list[str]]:
    """Returns (unused codes not in the allowlist, allowlisted codes that have a use)."""
    codes = read_codes(root)
    allowed = read_allowlist(root)
    used = used_identifiers(root, set(codes)) if codes else set()
    unused = [
        f"{ident} ({code})" for ident, code in codes.items()
        if ident not in used and ident not in allowed
    ]
    stale = sorted(ident for ident in allowed if ident in used)
    return unused, stale


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    unused, stale = find_problems(root)
    for item in unused:
        print(
            f"error code with no use in {', '.join(SOURCE_DIRS)}: {item}. Raise it, delete it, "
            f"or add it to {ALLOWLIST} with the reason")
    for item in stale:
        print(f"{item} has a use now: remove it from {ALLOWLIST}")
    return 1 if unused or stale else 0


if __name__ == "__main__":
    sys.exit(main())
