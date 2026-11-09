"""check_stx_skia_style accepts exactly the allowance and refuses every growth.

Each case builds a small stx_skia-shaped tree in a temporary directory, with a
file whose findings are known (one `auto`, one `->`), and runs the checker's
main over it.
"""
from __future__ import annotations
import contextlib
import io
from pathlib import Path
import tempfile
import unittest

import check_stx_skia_style

# Two findings: explicit-type and arrow-or-trailing-return.
TWO_FINDINGS: str = 'int f(int* p) {\n    auto v = 1;\n    return p->x + v;\n}\n'
CLEAN: str = 'int g(int value) {\n    return value;\n}\n'


class AllowanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory: tempfile.TemporaryDirectory[str] = tempfile.TemporaryDirectory()
        self.root: Path = Path(self.directory.name) / 'stx_skia'
        (self.root / 'skia' / 'src').mkdir(parents=True)
        (self.root / 'src').mkdir()
        (self.root / 'skia' / 'src' / 'imported.cpp').write_text(TWO_FINDINGS, encoding='utf-8')
        (self.root / 'src' / 'own.cpp').write_text(CLEAN, encoding='utf-8')

    def tearDown(self) -> None:
        self.directory.cleanup()

    def write_allowance(self, text: str) -> None:
        (self.root / check_stx_skia_style.ALLOWANCE_NAME).write_text(text, encoding='utf-8')

    def run_checker(self, extra: list[str] | None = None) -> tuple[int, str]:
        arguments: list[str] = ['--root', str(self.root)]
        if extra is not None:
            arguments.extend(extra)
        output: io.StringIO = io.StringIO()
        status: int = 0
        with contextlib.redirect_stdout(output):
            status = check_stx_skia_style.main(arguments)
        return status, output.getvalue()

    def test_exact_allowance_passes(self) -> None:
        self.write_allowance('# comment\n2 skia/src/imported.cpp\n')
        status, output = self.run_checker()
        self.assertEqual(status, 0, output)

    def test_unlisted_file_fails(self) -> None:
        self.write_allowance('2 skia/src/imported.cpp\n')
        (self.root / 'src' / 'own.cpp').write_text(TWO_FINDINGS, encoding='utf-8')
        status, output = self.run_checker()
        self.assertEqual(status, 1)
        self.assertIn('src/own.cpp: 2 findings in a file the allowance does not list', output)

    def test_raised_count_fails(self) -> None:
        self.write_allowance('1 skia/src/imported.cpp\n')
        status, output = self.run_checker()
        self.assertEqual(status, 1)
        self.assertIn('more than the 1 allowed', output)

    def test_lowered_count_fails_until_the_entry_is_lowered(self) -> None:
        self.write_allowance('3 skia/src/imported.cpp\n')
        status, output = self.run_checker()
        self.assertEqual(status, 1)
        self.assertIn('fewer than the 3 listed', output)

    def test_clean_listed_file_fails_until_removed(self) -> None:
        self.write_allowance('2 skia/src/imported.cpp\n1 src/own.cpp\n')
        status, output = self.run_checker()
        self.assertEqual(status, 1)
        self.assertIn('src/own.cpp: listed but has no findings', output)

    def test_malformed_line_is_refused(self) -> None:
        self.write_allowance('skia/src/imported.cpp 2\n')
        with self.assertRaises(ValueError):
            self.run_checker()

    def test_write_allowance_only_shrinks(self) -> None:
        self.write_allowance('5 skia/src/imported.cpp\n4 src/gone.cpp\n')
        status, output = self.run_checker(['--write-allowance'])
        self.assertEqual(status, 0, output)
        allowance: dict[str, int] = check_stx_skia_style.read_allowance(self.root / check_stx_skia_style.ALLOWANCE_NAME)
        self.assertEqual(allowance, {'skia/src/imported.cpp': 2})

    def test_write_allowance_refuses_growth(self) -> None:
        original: str = '1 skia/src/imported.cpp\n'
        self.write_allowance(original)
        status, output = self.run_checker(['--write-allowance'])
        self.assertEqual(status, 1)
        self.assertIn('may only shrink', output)
        written: str = (self.root / check_stx_skia_style.ALLOWANCE_NAME).read_text(encoding='utf-8')
        self.assertEqual(written, original)


if __name__ == '__main__':
    unittest.main()
