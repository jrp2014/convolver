#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Formatter discovery accepts new stable releases without a hard-coded major."""

import importlib.util
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'check_format', Path(__file__).resolve().parents[1] / 'scripts/check_format.py'
)
format_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(format_check)


class FormatterTests(unittest.TestCase):
    def version_output(self, output):
        with patch.object(
            subprocess, 'run', return_value=subprocess.CompletedProcess(['clang-format', '--version'], 0, stdout=output)
        ):
            return format_check.major_version('clang-format')

    def test_accepts_future_stable_release(self):
        with patch.dict(os.environ, {'CLANG_FORMAT': '/tools/clang-format'}, clear=True), patch.object(
            format_check, 'major_version', return_value='99'
        ):
            self.assertEqual(format_check.find_clang_format(), '/tools/clang-format')

    def test_rejects_development_and_release_candidate_binaries(self):
        for suffix in ('git', '-rc1', 'rc2', 'dev'):
            with self.subTest(suffix=suffix):
                self.assertIsNone(self.version_output('clang-format version 99.0.0' + suffix))

    def test_recognises_stable_release_with_build_provenance(self):
        self.assertEqual(self.version_output('clang-format version 99.1.2 (https://llvm.org)'), '99')

    def test_explicit_invalid_binary_does_not_fall_back(self):
        with patch.dict(os.environ, {'CLANG_FORMAT': '/bad/formatter'}, clear=True), patch.object(
            format_check, 'major_version', return_value=None
        ), patch.object(format_check.shutil, 'which') as lookup:
            with self.assertRaises(SystemExit):
                format_check.find_clang_format()
            lookup.assert_not_called()

    def test_handles_missing_executable(self):
        with patch.object(subprocess, 'run', side_effect=FileNotFoundError):
            self.assertIsNone(format_check.major_version('/missing/clang-format'))


if __name__ == '__main__':
    unittest.main()
