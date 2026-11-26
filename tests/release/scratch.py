# SPDX-License-Identifier: GPL-2.0-or-later
"""A temporary folder for the package checks that survives Windows holding a
just-run program for a moment after it exits.

On Windows 11 (seen on GitHub's Windows 11 on Arm runner, REL-11) a program
that has just exited can stay locked for a short while, for example while
the antivirus scans it or x64 emulation caches its translation, so deleting
the folder at once fails with "Access is denied". The check's own cleanup
waits and retries for up to a minute; anything still locked then is an
error, as before.
"""

import contextlib
import shutil
import sys
import tempfile
import time

ATTEMPTS = 120
PAUSE = 0.5


def remove_tree(path):
    for attempt in range(ATTEMPTS):
        try:
            shutil.rmtree(path)
            return
        except PermissionError:
            if sys.platform != 'win32' or attempt == ATTEMPTS - 1:
                raise
            time.sleep(PAUSE)


@contextlib.contextmanager
def temporary_directory(prefix):
    """As tempfile.TemporaryDirectory: yields the folder's name as a string."""
    name = tempfile.mkdtemp(prefix=prefix)
    try:
        yield name
    finally:
        remove_tree(name)
