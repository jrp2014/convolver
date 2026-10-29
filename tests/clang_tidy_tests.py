#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The parallel clang-tidy runner analyses every file and reports each failure.

A stand-in clang-tidy (a small Python script) records its arguments; files
named bad*.cpp fail. The concurrency test has each run wait until all of them
have started, so it passes only if they really run side by side.
"""

import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/clang_tidy.py'

FAKE = textwrap.dedent("""\
    import os, sys, time
    from pathlib import Path
    build, source = sys.argv[2], sys.argv[3]
    assert sys.argv[1] == '-p', sys.argv
    marks = Path(os.environ['FAKE_MARKS'])
    (marks / Path(source).name).touch()
    wait_for = int(os.environ.get('FAKE_WAIT_FOR', '0'))
    deadline = time.monotonic() + 20
    while len(list(marks.iterdir())) < wait_for:
        if time.monotonic() > deadline:
            print(f'{source}: the other runs never started')
            sys.exit(3)
        time.sleep(0.01)
    print(f'{source}: checked with {build}')
    if Path(source).name.startswith('bad'):
        print(f'{source}:1:1: error: a finding [some-check]')
        sys.exit(1)
""")


@unittest.skipIf(os.name == 'nt', 'the stand-in clang-tidy is a script with a #! line')
class ClangTidyRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name)
        self.marks = root / 'marks'
        self.marks.mkdir()
        self.fake = root / 'clang-tidy'
        self.fake.write_text(f'#!{sys.executable}\n' + FAKE)
        self.fake.chmod(0o755)

    def tearDown(self):
        self.directory.cleanup()

    def run_script(self, *arguments, wait_for=0):
        environment = dict(os.environ, FAKE_MARKS=str(self.marks), FAKE_WAIT_FOR=str(wait_for))
        return subprocess.run(
            [sys.executable, str(SCRIPT), '--clang-tidy', str(self.fake), '-p', 'build-x', *arguments],
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_every_file_is_analysed_and_printed_in_order(self):
        sources = [f'portable/file{n}.cpp' for n in range(6)]
        result = self.run_script('--jobs', '3', *sources)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        positions = [result.stdout.index(f'{source}: checked with build-x') for source in sources]
        self.assertEqual(positions, sorted(positions))
        self.assertIn('clang-tidy passed on all 6 files', result.stdout)

    def test_a_failure_is_reported_and_the_other_files_still_run(self):
        sources = ['portable/a.cpp', 'portable/bad1.cpp', 'portable/c.cpp', 'portable/bad2.cpp']
        result = self.run_script('--jobs', '2', *sources)
        self.assertEqual(result.returncode, 1)
        for source in sources:
            self.assertIn(f'{source}: checked with build-x', result.stdout)
        self.assertIn('portable/bad1.cpp:1:1: error: a finding [some-check]', result.stdout)
        self.assertIn('--- portable/bad1.cpp: FAILED (exit status 1)', result.stdout)
        self.assertIn('clang-tidy failed on 2 of 4 files: portable/bad1.cpp portable/bad2.cpp', result.stdout)

    def test_files_run_side_by_side(self):
        sources = [f'portable/file{n}.cpp' for n in range(4)]
        result = self.run_script('--jobs', '4', *sources, wait_for=4)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_missing_clang_tidy_is_an_error(self):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), '--clang-tidy', 'no-such-clang-tidy', '-p', 'build', 'portable/engine.cpp'],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn('cannot find no-such-clang-tidy', result.stderr)


if __name__ == '__main__':
    unittest.main()
