# SPDX-License-Identifier: GPL-2.0-or-later
"""The release scripts' `--jobs` limit (CI-16).

Builds and tests use every CPU by default (CI-14); `--jobs N` lowers that for
a machine with less memory. These tests check the default, the validation, the
option on each script, and that the dependency build passes the limit to CMake.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import build_dependencies  # noqa: E402

SCRIPTS = (
    ROOT / 'scripts/release/candidate.py',
    ROOT / 'scripts/release/build_dependencies.py',
    ROOT / 'tests/release/source_build.py',
)


class JobsTests(unittest.TestCase):
    def test_default_is_every_cpu(self):
        self.assertEqual(build_dependencies.parallel(), ['--parallel', str(os.cpu_count() or 2)])
        self.assertEqual(build_dependencies.parallel(None), build_dependencies.parallel())

    def test_limit(self):
        self.assertEqual(build_dependencies.parallel(1), ['--parallel', '1'])
        self.assertEqual(build_dependencies.parallel(3), ['--parallel', '3'])

    def test_argument_validation(self):
        self.assertEqual(build_dependencies.jobs_argument('4'), 4)
        for text in ('0', '-2', 'two', '', '1.5'):
            with self.subTest(text=text), self.assertRaises(argparse.ArgumentTypeError):
                build_dependencies.jobs_argument(text)

    def test_each_script_offers_jobs(self):
        for script in SCRIPTS:
            with self.subTest(script=script.name):
                shown = subprocess.run([sys.executable, str(script), '--help'], capture_output=True, text=True,
                                       timeout=60)
                self.assertEqual(shown.returncode, 0, shown.stderr)
                self.assertIn('--jobs N', shown.stdout)
                refused = subprocess.run([sys.executable, str(script), '--jobs', '0'], capture_output=True,
                                         text=True, timeout=60)
                self.assertEqual(refused.returncode, 2, refused.stderr)
                self.assertIn("--jobs: must be a whole number, 1 or more, not '0'", refused.stderr)

    def test_dependency_build_passes_the_limit(self):
        policy = {'dependencies': {'libsndfile': {'directory': 'libsndfile', 'version': '1.0'}}}

        def unpack(policy, cache, source, fetch, names):
            (source / 'libsndfile').mkdir(parents=True)
            (source / 'libsndfile/COPYING').write_text('licence')

        for jobs, expected in ((None, str(os.cpu_count() or 2)), (2, '2')):
            with self.subTest(jobs=jobs), tempfile.TemporaryDirectory() as temporary, \
                    mock.patch.object(build_dependencies, 'unpack_dependencies', unpack), \
                    mock.patch.object(build_dependencies.subprocess, 'run') as run:
                build_dependencies.build(policy, Path(temporary) / 'cache', Path(temporary) / 'work',
                                         backend='OOURA', jobs=jobs)
                builds = [call.args[0] for call in run.call_args_list if '--build' in call.args[0]]
                self.assertEqual(len(builds), 1)
                self.assertEqual(builds[0][-2:], ['--parallel', expected])


if __name__ == '__main__':
    unittest.main()
