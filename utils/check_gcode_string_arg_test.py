"""Tests of check_gcode_string_arg.py. Run: python3 -m unittest utils/check_gcode_string_arg_test.py"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import check_gcode_string_arg as check  # noqa: E402

# M340 as it was before the null check: "M340" with no argument passed null to sscanf().
UNCHECKED = """\
void PrusaGcodeSuite::M340() {
    const auto read = sscanf(parser.string_arg, "%20s %i", host.data(), &port);
}
"""

CHECKED_FORMS = """\
void PrusaGcodeSuite::M330() {
    if (!parser.string_arg) {
        return;
    }
    strcmp(metric->name, parser.string_arg);
}

void PrusaGcodeSuite::M334() {
    const auto n = parser.string_arg ? sscanf(parser.string_arg, "%20s", host.data()) : 0;
}

void PrusaGcodeSuite::M862_3() {
    if (parser.boolval('P') && parser.string_arg) {
        const char *arg = parser.string_arg;
    }
}

void GcodeSuite::M117() {
    if (parser.string_arg && parser.string_arg[0]) {
    }
}

void GcodeSuite::T(const int8_t tool_index) {
    if (parser.string_arg) {
    }
}
"""

# The parser always sets string_arg for M23, but M32 calls M23 and has no string_arg.
ALWAYS_SET = """\
void GcodeSuite::M23() {
    for (char *fn = parser.string_arg; *fn; ++fn) {
    }
}
"""

# A statement and a function with a pointer return type that DEFINITION must handle.
SPLIT = """\
static const char *name() {
    return parser.string_arg;
}

void GcodeSuite::M340() {
    if (x)
    {
    }
    else if (parser.string_arg)
    {
    }
}
"""


class CheckGcodeStringArgTest(unittest.TestCase):

    def make_repo(self, root: pathlib.Path, files: dict[str, str]) -> None:
        (root / "src").mkdir()
        for name, text in files.items():
            (root / "src" / name).write_text(text)

    def problems(self, files: dict[str, str]) -> tuple[list[str], list[str]]:
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.make_repo(root, files)
            return check.find_problems(root)

    def test_unchecked_use_fails(self):
        unchecked, _ = self.problems({"M340.cpp": UNCHECKED})
        self.assertEqual(unchecked, ["src/M340.cpp:1: M340"])

    def test_checked_uses_pass(self):
        unchecked, _ = self.problems({"checked.cpp": CHECKED_FORMS})
        self.assertEqual(unchecked, [])

    def test_a_check_in_another_function_does_not_count(self):
        unchecked, _ = self.problems({"both.cpp": CHECKED_FORMS + UNCHECKED})
        line = CHECKED_FORMS.count("\n") + 1
        self.assertEqual(unchecked, [f"src/both.cpp:{line}: M340"])

    def test_a_code_that_the_parser_always_gives_a_string_arg_needs_a_check(
            self):
        unchecked, _ = self.problems({"M23.cpp": ALWAYS_SET})
        self.assertEqual(unchecked, ["src/M23.cpp:1: M23"])

    def test_a_check_in_the_next_function_does_not_count(self):
        # The pointer return type of name() and the "else if" line do not hide or split a function.
        unchecked, _ = self.problems({"split.cpp": SPLIT})
        self.assertEqual(unchecked, ["src/split.cpp:1: name"])

    def test_allowlisted_function_passes(self):
        text = "".join(
            UNCHECKED.replace("M340", name) for name in check.ALLOWLIST)
        self.assertEqual(self.problems({"allowed.cpp": text}), ([], []))

    def test_allowlisted_function_that_does_not_read_it_must_leave_the_list(
            self):
        self.assertEqual(self.problems({"M340.cpp": CHECKED_FORMS}),
                         ([], ["M0_M1", "M16"]))


if __name__ == "__main__":
    unittest.main()
