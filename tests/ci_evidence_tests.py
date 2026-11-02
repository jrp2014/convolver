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
        ]
        left = ['two/test_binary', 'first-use/output.wav', 'two/other.log']
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder) / 'build'
            for name in kept + left:
                (build / name).parent.mkdir(parents=True, exist_ok=True)
                (build / name).write_text(name)
            copied = ci_evidence.collect(build, Path(folder) / 'evidence')
            self.assertEqual(sorted(Path(name).as_posix() for name in copied), sorted(kept))
            for name in kept:
                self.assertEqual((Path(folder) / 'evidence' / name).read_text(), name)


if __name__ == '__main__':
    sys.exit(unittest.main())
