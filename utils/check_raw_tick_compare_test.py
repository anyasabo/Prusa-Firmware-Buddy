"""Tests of check_raw_tick_compare.py. Run: python3 -m unittest utils/check_raw_tick_compare_test.py"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import check_raw_tick_compare as check  # noqa: E402

# Each line is wrong near the wrap of the counter.
RAW = """\
if (ticks_ms() >= start + timeout) {
return _millis() >= (lastUARTActivityMs + timeout);
return until_ms > ticks_ms();
if (millis() > timeout || planner.draining()) {
if (buffer_overflown && (reported + 5000U) < ticks_ms()) {
return freertos::millis() < deadline;
while (ticks_us() <= end_us) {
if (last_ticks_ms() > deadline) {
if (static_cast<uint32_t>(ticks_ms()) > deadline) {
if (ticks_ms() + 10 > deadline) {
while (osKernelGetTickCount() < end) {
if (osKernelSysTick() >= deadline) {
"""

CORRECT = """\
if (ticks_ms() - start >= timeout) {
if (ticks_diff(deadline, ticks_ms()) > 0) {
if (ELAPSED(millis(), timeout)) {
uint32_t now_us = static_cast<uint32_t>(ticks_us());
return (static_cast<uint64_t>(nonce)) << 32 | (static_cast<uint64_t>(ticks_s()));
if (timeout > ticks_ms() - start) {
// Do not write ticks_ms() >= start + timeout.
const auto now = ticks_ms();
if (last_ticks_ms() - start >= timeout) {
"""


class RawComparesTest(unittest.TestCase):

    def test_raw_comparisons_are_found(self):
        found = check.raw_compares(RAW)
        self.assertEqual([line for line, _ in found],
                         list(range(1,
                                    len(RAW.splitlines()) + 1)))

    def test_correct_forms_are_not_found(self):
        self.assertEqual(check.raw_compares(CORRECT), [])


class FindProblemsTest(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        (self.root / "src").mkdir()
        (self.root / "src" / "a.cpp"
         ).write_text("bool f() {\n    return until_ms > ticks_ms();\n}\n")
        (self.root / "lib/WUI/sntp").mkdir(parents=True)
        (self.root /
         "lib/WUI/sntp/sntp.c").write_text("if (millis() > t) {}\n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_problem_has_path_and_line(self):
        problems, stale = check.find_problems(self.root, {})
        self.assertEqual(problems,
                         ["src/a.cpp:2: return until_ms > ticks_ms();"])
        self.assertEqual(stale, [])

    def test_allowlist_hides_the_site(self):
        allow = {"src/a.cpp:return until_ms > ticks_ms();": "reason"}
        problems, stale = check.find_problems(self.root, allow)
        self.assertEqual(problems, [])
        self.assertEqual(stale, [])

    def test_allowlist_key_has_no_comment(self):
        (self.root / "src" /
         "a.cpp").write_text("return until_ms > ticks_ms(); // a reason\n")
        allow = {"src/a.cpp:return until_ms > ticks_ms();": "reason"}
        problems, stale = check.find_problems(self.root, allow)
        self.assertEqual(problems, [])
        self.assertEqual(stale, [])

    def test_include_dir_is_checked(self):
        (self.root / "include").mkdir()
        (self.root / "include" /
         "b.hpp").write_text("return x > ticks_ms();\n")
        problems, _ = check.find_problems(self.root, {})
        self.assertIn("include/b.hpp:1: return x > ticks_ms();", problems)

    def test_stale_allowlist_entry_is_reported(self):
        allow = {"src/b.cpp:return x > ticks_ms();": "reason"}
        _, stale = check.find_problems(self.root, allow)
        self.assertEqual(stale, ["src/b.cpp:return x > ticks_ms();"])


if __name__ == "__main__":
    unittest.main()
