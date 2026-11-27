"""Tests of check_gcode_string_arg.py. Run: python3 -m unittest utils/check_gcode_string_arg_test.py"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import check_gcode_string_arg as check  # noqa: E402

PARSER = """\
  // Only use string_arg for these M codes
  if (letter == 'M') switch (codenum) {
    #if ENABLED(EXPECTED_PRINTER_CHECK)
      case 16:
    #endif
    case 23: case 117: string_arg = p; return;
    default: break;
  }
"""

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

# The parser always sets string_arg for M23.
ALWAYS_SET = """\
void GcodeSuite::M23() {
    for (char *fn = parser.string_arg; *fn; ++fn) {
    }
}
"""


class CheckGcodeStringArgTest(unittest.TestCase):

    def make_repo(self, root: pathlib.Path, files: dict[str, str]) -> None:
        parser = root / check.PARSER
        parser.parent.mkdir(parents=True)
        parser.write_text(PARSER)
        (root / "src").mkdir()
        for name, text in files.items():
            (root / "src" / name).write_text(text)

    def problems(self, files: dict[str, str]) -> tuple[list[str], list[str]]:
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.make_repo(root, files)
            return check.find_problems(root)

    def test_reads_the_codes_that_always_have_a_string_arg(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.make_repo(root, {})
            self.assertEqual(check.always_set_codes(root),
                             {"M16", "M23", "M117"})

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

    def test_a_code_that_always_has_a_string_arg_passes(self):
        unchecked, _ = self.problems({"M23.cpp": ALWAYS_SET})
        self.assertEqual(unchecked, [])

    def test_allowlisted_function_passes(self):
        text = UNCHECKED.replace("M340", "M0_M1")
        self.assertEqual(self.problems({"M0_M1.cpp": text}), ([], []))

    def test_allowlisted_function_that_does_not_read_it_must_leave_the_list(
            self):
        self.assertEqual(self.problems({"M23.cpp": ALWAYS_SET}),
                         ([], ["M0_M1"]))

    def test_missing_case_list_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.make_repo(root, {})
            (root / check.PARSER).write_text("void parse() {}\n")
            with self.assertRaises(RuntimeError):
                check.always_set_codes(root)


if __name__ == "__main__":
    unittest.main()
