#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for ardour_tests.py's rule for Ardour 9.0's new_session abort (HOST-7).

The Ardour job accepts only one failure of ardourN-new_session: Ardour 9.0's
abort as it exits, after creating the session. These check that rule without
Ardour, in pre-commit, so that a wider rule cannot hide another failure.
"""

import importlib.util
import pathlib
import signal
import sys
import unittest

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('ardour_tests', HERE / 'ardour_tests.py')
ardour_tests = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ardour_tests)

ABORTED = -signal.SIGABRT
KNOWN = (
    ': [WARNING]: No synth for midi-audition found.\n'
    "Created session in '/tmp/sessions/mono'\n"
    'FATAL: exception not rethrown\n'
)


def accepted(returncode=ABORTED, output=KNOWN, version=(9, 0), *, session_saved=True):
    return ardour_tests.known_new_session_abort(returncode, output, version, session_saved)


class NewSessionAbort(unittest.TestCase):
    def test_known_abort_on_ardour_9_0(self):
        self.assertTrue(accepted())

    def test_other_abort_message(self):
        other = KNOWN.replace('FATAL: exception not rethrown', "Assertion `x' failed.")
        self.assertFalse(accepted(output=other))

    def test_abort_before_the_session_is_created(self):
        before = "FATAL: exception not rethrown\nCreated session in '/tmp/sessions/mono'\n"
        self.assertFalse(accepted(output=before))

    def test_no_created_line(self):
        self.assertFalse(accepted(output='FATAL: exception not rethrown\n'))

    def test_other_versions(self):
        for version in ((8, 4), (9, 1), (10, 0), None):
            with self.subTest(version=version):
                self.assertFalse(accepted(version=version))

    def test_other_exit(self):
        for returncode in (1, -signal.SIGSEGV, 134):
            with self.subTest(returncode=returncode):
                self.assertFalse(accepted(returncode=returncode))

    def test_no_session_file(self):
        self.assertFalse(accepted(session_saved=False))


if __name__ == '__main__':
    unittest.main()
