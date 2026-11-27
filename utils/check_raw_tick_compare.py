#!/usr/bin/env python3
"""Fails if code compares a tick counter with <, >, <= or >=.

ticks_ms(), millis() and the other tick counters are uint32_t. They wrap to 0 each 2^32 ticks
(49.7 days for a millisecond counter, 71.6 minutes for a microsecond counter). A comparison such as
"ticks_ms() >= start + timeout" or "deadline > ticks_ms()" is wrong near the wrap: the sum wraps
first, or the counter wraps first. Then a timeout occurs at once, or it does not occur for 49.7 days.

Compare the elapsed time instead. Unsigned subtraction is correct across the wrap:

- "ticks_ms() - start >= timeout"
- "ticks_diff(deadline, ticks_ms()) > 0" (timing.h)
- "ELAPSED(millis(), deadline)" or "PENDING(millis(), deadline)" (Marlin, millis_t.h)

The check finds a tick call with a comparison operator directly before or after it, also through a
cast or a "+ N" sum on its left side. It does not find a comparison of a variable that holds a tick
value, for example "last + period > now" where now is a tick value. A site where the comparison is correct
goes in ALLOWLIST as "path:code" (the line without the indent and the trailing comment), with the
reason. A listed site that
the check no longer finds must leave the list, so the list stays current.

Usage: python3 utils/check_raw_tick_compare.py [REPO_ROOT]
"""

import pathlib
import re
import sys

SOURCE_DIRS = ("src", "include", "lib/Marlin/Marlin/src", "lib/WUI")
# Vendored code (lib/sbom.yml).
SKIP_DIRS = ("lib/WUI/mdns", "lib/WUI/sntp")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}

# "path:code": the reason why the comparison is correct there.
ALLOWLIST: dict[str, str] = {
    "src/puppy/xbuddy_extension/mmu.cpp:return freertos::millis() >= (lastUARTActivityMs + timeout);":
    "wrong, a separate change fixes the xBuddy extension timeouts. Then this entry is stale and must go",
}

TICK = (
    r"(?:\b\w+::)?\b(?:(?:last_)?ticks_(?:ms|us)|_?millis|micros|HAL_GetTick|xTaskGetTickCount"
    r"|osKernelSysTick|osKernelGetTickCount)\(\)")
# clang-format puts spaces around a binary operator, and none before the '>' of a template.
OPERATOR = r"(?:<|>)=?"
# ticks_s() is not in the list: it wraps after 136 years.
CAST = r"static_cast<[\w:]+>\(\s*" + TICK + r"\s*\)"
# A "+ N" sum can come between the call and the operator.
TICK_LEFT = re.compile(r"(?:" + CAST + r"|" + TICK +
                       r")(?:\s*\+\s*[\w:]+)?\s+" + OPERATOR + r"\s")
# "x > ticks_ms() - start" is a correct form.
TICK_RIGHT = re.compile(r"\s" + OPERATOR + r"\s+\(?\s*" + TICK + r"(?!\s*-)")
COMMENT = re.compile(r"//.*$")


def raw_compares(text: str) -> list[tuple[int, str]]:
    """Returns (line, code) for each line that compares a tick call with <, >, <= or >=."""
    found = []
    for number, line in enumerate(text.splitlines(), start=1):
        code = COMMENT.sub("", line)
        if TICK_LEFT.search(code) or TICK_RIGHT.search(code):
            found.append((number, code.strip()))
    return found


def find_problems(
        root: pathlib.Path,
        allowlist: dict[str, str] = ALLOWLIST) -> tuple[list[str], list[str]]:
    """Returns (raw tick comparisons, allowlisted sites that the check does not find)."""
    problems = []
    allowed_used = set()
    for top in SOURCE_DIRS:
        for path in sorted((root / top).rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            if rel.startswith(SKIP_DIRS):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for line, code in raw_compares(text):
                key = f"{rel}:{code}"
                if key in allowlist:
                    allowed_used.add(key)
                else:
                    problems.append(f"{rel}:{line}: {code}")
    stale = sorted(set(allowlist) - allowed_used)
    return problems, stale


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    problems, stale = find_problems(root)
    for item in problems:
        print(
            f"{item}: a tick counter wraps each 2^32 ticks, so this comparison is wrong near the "
            f"wrap. Compare the elapsed time: ticks_ms() - start >= timeout, or use ticks_diff()"
        )
    for key in stale:
        print(
            f"{key}: the check does not find this site now: remove it from ALLOWLIST in {__file__}"
        )
    return 1 if problems or stale else 0


if __name__ == "__main__":
    sys.exit(main())
