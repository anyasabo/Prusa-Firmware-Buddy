"""Tests of check_gcode_seen_value.py. Run: python3 -m unittest utils/check_gcode_seen_value_test.py"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import check_gcode_seen_value as check  # noqa: E402

# M593 as it was: "M593 X F" set the input shaper frequency to 0.
UNCHECKED = """\
void GcodeSuite::M593() {
    if (parser.seen('F')) {
        const float f = parser.value_float();
        params.axis.frequency = f;
    }
    if (parser.seen('P')) thermalManager.temp_bed.pid.Kp = parser.value_float();
    g29_verbose_level = parser.seen('V') ? parser.value_int() : 0;
}
"""

CHECKED_FORMS = """\
void GcodeSuite::M593() {
    if (parser.seenval('F')) {
        params.axis.frequency = parser.value_float();
    }
    if (parser.seen('S')) {
        if (parser.has_value()) {
            interval = parser.value_ulong();
        }
    }
    if (parser.seen('W')) {
        store = parser.value_bool();
    }
    if (parser.seen('X')) {
        seen_x = true;
    }
    if (parser.seen('Y')) {
        if (parser.seen('Z')) {
        }
    }
    const int v = parser.seen('V') ? 1 : 0;
}
"""


class FindTest(unittest.TestCase):

    def test_unchecked_reads(self):
        self.assertEqual(check.unchecked_reads(UNCHECKED), [(2, "F"), (6, "P"),
                                                            (7, "V")])

    def test_checked_forms(self):
        self.assertEqual(check.unchecked_reads(CHECKED_FORMS), [])

    def test_allowlist(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "src").mkdir()
            (root / "src" / "M593.cpp").write_text(UNCHECKED)
            saved = dict(check.ALLOWLIST)
            try:
                check.ALLOWLIST.clear()
                check.ALLOWLIST["src/M593.cpp:P"] = "test"
                check.ALLOWLIST["src/M593.cpp:Q"] = "test"
                problems, stale = check.find_problems(root)
            finally:
                check.ALLOWLIST.clear()
                check.ALLOWLIST.update(saved)
            self.assertEqual(problems,
                             ["src/M593.cpp:2: F", "src/M593.cpp:7: V"])
            self.assertEqual(stale, ["src/M593.cpp:Q"])


if __name__ == "__main__":
    unittest.main()
