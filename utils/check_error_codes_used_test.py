"""Tests of check_error_codes_used.py. Run: python3 -m unittest utils/check_error_codes_used_test.py"""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import check_error_codes_used as check  # noqa: E402

YAML = """\
Errors:
  - code: "XX528"
    title: "LOADCELL TIMEOUT"
    id: "LOADCELL_TIMEOUT"

  - code: "XX101"
    title: "USED"
    id: "USED_ONE"

  - code: "XX301"
    title: "ALLOWED"
    id: "ALLOWED_ONE"

  - code: "XX302"
    title: "OLD"
    id: "OLD_ONE"
    deprecated: true

  - code: "XX601"
    title: "BOOTLOADER"
    id: "BOOT_ONE"
"""


class CheckErrorCodesUsedTest(unittest.TestCase):

    def make_repo(self, root: pathlib.Path, source: str,
                  allowlist: str) -> None:
        (root / "lib/Prusa-Error-Codes/yaml").mkdir(parents=True)
        (root /
         "lib/Prusa-Error-Codes/yaml/buddy-error-codes.yaml").write_text(YAML)
        (root / "src").mkdir()
        (root / "src/a.cpp").write_text(source)
        (root / "utils").mkdir()
        (root / "utils/error_codes_unused_allowlist.txt").write_text(allowlist)

    def test_reads_the_identifiers(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.make_repo(root, "", "")
            codes = check.read_codes(root)
            self.assertEqual(
                codes, {
                    "ERR_SYSTEM_LOADCELL_TIMEOUT": "XX528",
                    "ERR_MECHANICAL_USED_ONE": "XX101",
                    "ERR_ELECTRO_ALLOWED_ONE": "XX301",
                })

    def test_unused_code_fails(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.make_repo(root, "raise(ErrCode::ERR_MECHANICAL_USED_ONE);",
                           "ERR_ELECTRO_ALLOWED_ONE  # reason\n")
            self.assertEqual(check.find_problems(root),
                             (["ERR_SYSTEM_LOADCELL_TIMEOUT (XX528)"], []))

    def test_allowlisted_code_that_is_used_must_leave_the_list(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.make_repo(
                root, "ERR_MECHANICAL_USED_ONE ERR_SYSTEM_LOADCELL_TIMEOUT "
                "ERR_ELECTRO_ALLOWED_ONE", "ERR_ELECTRO_ALLOWED_ONE\n")
            self.assertEqual(check.find_problems(root),
                             ([], ["ERR_ELECTRO_ALLOWED_ONE"]))

    def test_a_longer_identifier_does_not_count(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            self.make_repo(
                root, "ERR_MECHANICAL_USED_ONE "
                "ERR_SYSTEM_LOADCELL_TIMEOUT_2 ERR_ELECTRO_ALLOWED_ONE", "")
            self.assertEqual(
                check.find_problems(root)[0],
                ["ERR_SYSTEM_LOADCELL_TIMEOUT (XX528)"])


if __name__ == "__main__":
    unittest.main()
