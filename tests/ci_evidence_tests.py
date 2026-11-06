#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for scripts/ci_evidence.py: what it collects from a build folder.

The Audio Unit job checks the same after a real abort and forced end
(au.log_evidence, CI-27); this runs everywhere, in pre-commit.
"""

import importlib.util
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('ci_evidence', ROOT / 'scripts' / 'ci_evidence.py')
ci_evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ci_evidence)


class Collect(unittest.TestCase):
    def test_logs_and_rotated_logs(self):
        kept = [
            'Testing/Temporary/LastTest.log',
            'junit.xml',
            'CMakeCache.txt',
            'one/convolver-tests.log',
            'two/convolver-adapter-tests.log',
            'two/convolver-adapter-tests.log.old',
            'two/convolver-editor-tests.log',
            'vst3-load-timing/VST3-load-timing.json',
        ]
        left = ['two/test_binary', 'first-use/output.wav', 'two/other.log', 'vst3-load-timing/x.cfg']
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder) / 'build'
            for name in kept + left:
                (build / name).parent.mkdir(parents=True, exist_ok=True)
                (build / name).write_text(name)
            copied = ci_evidence.collect(build, Path(folder) / 'evidence')
            self.assertEqual(sorted(Path(name).as_posix() for name in copied), sorted(kept))
            for name in kept:
                self.assertEqual((Path(folder) / 'evidence' / name).read_text(), name)


class Summary(unittest.TestCase):
    def summary(self, *arguments):
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder) / 'build'
            (build / 'Testing/Temporary').mkdir(parents=True)
            (build / 'Testing/Temporary/LastTestsFailed.log').write_text('3:vst3.pluginval\n')
            summary = Path(folder) / 'summary.md'
            environment = {'GITHUB_STEP_SUMMARY': str(summary), 'GITHUB_RUN_ATTEMPT': '2'}
            with unittest.mock.patch.dict(ci_evidence.os.environ, environment):
                code = ci_evidence.main(['ci_evidence.py', *arguments, str(Path(folder) / 'out'), str(build)])
            self.assertEqual(code, 0)
            self.assertTrue((Path(folder) / 'out' / 'build/Testing/Temporary/LastTestsFailed.log').is_file())
            return summary.read_text()

    def test_failure(self):
        text = self.summary()
        self.assertIn('### Failed on attempt 2', text)
        self.assertIn('`vst3.pluginval`', text)

    # CI-8: an advisory check's evidence is kept without calling the job failed.
    def test_advisory(self):
        text = self.summary('--advisory')
        self.assertIn('### Advisory problem on attempt 2', text)
        self.assertIn('`vst3.pluginval`', text)
        self.assertNotIn('Failed', text)

    def test_advisory_junit(self):
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder) / 'build'
            build.mkdir()
            (build / 'junit.xml').write_text('formal')
            (build / 'advisory-junit.xml').write_text('advisory')
            copied = ci_evidence.collect(build, Path(folder) / 'out', advisory=True)
            self.assertEqual(copied, ['advisory-junit.xml'])


if __name__ == '__main__':
    sys.exit(unittest.main())
