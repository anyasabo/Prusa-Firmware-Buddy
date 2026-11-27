"""Tests of check_gcode_seen_value.py. Run: python3 -m unittest utils/check_gcode_seen_value_test.py"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import check_gcode_seen_value as check  # noqa: E402

# Each form reads 0 for a letter with no value, for example "M593 X F".
UNCHECKED = """\
void GcodeSuite::M593() {
    if (parser.seen('F')) {
        params.axis.frequency = parser.value_float();
    }
    if (parser.seen('P')) thermalManager.temp_bed.pid.Kp = parser.value_float();
    g29_verbose_level = parser.seen('V') ? parser.value_int() : 0;
    const auto len = parser.seen('U') ? std::optional<float>(parser.value_axis_units(E_AXIS)) : std::nullopt;
    const bool seen_s = parser.seen('S');
    if (seen_s) {
        pressure_advance = parser.value_float();
    }
    const bool seen_l = parser.seen('L');
    const uint8_t n_legs = seen_l ? parser.value_byte() : 0;
    LOOP_XYZ(i) if (parser.seen(axis_codes[i])) set_home_offset(i, parser.value_linear_units());
    if (parser.seen('Q')) {
        const bool q = parser.seen('R');
        f = parser.value_float();
    }
    if (!parser.seen('I')) return;
    const page_idx_t page_idx = parser.value_ulong();
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
        if (parser.seenval('Z')) {
            z = parser.value_float();
        }
    }
    const int v = parser.seen('V') ? 1 : 0;
    const bool seen_d = parser.seen('D');
    if (seen_d) {
        report();
    }
    if (!parser.seen('I')) return;
    if (!parser.has_value()) return;
    const page_idx_t page_idx = parser.value_ulong();
}
"""


class FindTest(unittest.TestCase):

    def test_unchecked_reads(self):
        self.assertEqual(check.unchecked_reads(UNCHECKED),
                         [(2, "'F'"), (5, "'P'"), (6, "'V'"), (7, "'U'"),
                          (8, "'S'"), (12, "'L'"), (14, "axis_codes[i]"),
                          (15, "'Q'"), (19, "'I'")])

    def test_checked_forms(self):
        self.assertEqual(check.unchecked_reads(CHECKED_FORMS), [])

    def test_allowlist(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "src").mkdir()
            (root / "src" / "M593.cpp").write_text(UNCHECKED)
            problems, stale = check.find_problems(root, {
                "M593:'P'": "test",
                "M593:'Z'": "test",
                "M201:'F'": "test"
            })
            self.assertNotIn("src/M593.cpp:5: M593: 'P'", problems)
            self.assertIn("src/M593.cpp:2: M593: 'F'", problems)
            self.assertEqual(stale, ["M201:'F'", "M593:'Z'"])


if __name__ == "__main__":
    unittest.main()
