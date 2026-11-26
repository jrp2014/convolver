# SPDX-License-Identifier: GPL-2.0-or-later
"""A temporary folder for the package checks that survives Windows holding a
just-run program for a moment after it exits.

On GitHub's Windows 11 on Arm runner (REL-11) a program that had just run
and exited was still locked when the check deleted its temporary copy, so
the removal failed with "Access is denied" (run 45876746943; the cause, for
example antivirus scanning, was not identified). On Windows the check's own
cleanup waits and retries for up to a minute, saying so in the log; anything
still locked then is an error, as before. Elsewhere it fails at once.
"""

import contextlib
import shutil
import stat
import sys
import tempfile
import time
from pathlib import Path

ATTEMPTS = 120
PAUSE = 0.5


def removed(path):
    """Removes the folder; False if Windows still holds a file in it."""
    try:
        shutil.rmtree(path)
    except PermissionError:
        if sys.platform != 'win32':
            raise
        make_writable(path)
        return False
    return True


def make_writable(path):
    """Clears read-only marks, as tempfile.TemporaryDirectory's cleanup does."""
    for item in Path(path).rglob('*'):
        with contextlib.suppress(OSError):
            item.chmod(stat.S_IWRITE | stat.S_IREAD)


def remove_tree(path):
    for attempt in range(ATTEMPTS - 1):
        if removed(path):
            if attempt:
                print(f'Removed {path} after {attempt * PAUSE:g} s', flush=True)
            return
        if not attempt:
            print(f'Windows still holds a file in {path}; retrying for up to a minute', flush=True)
        time.sleep(PAUSE)
    # The last attempt raises whatever still holds the folder.
    shutil.rmtree(path)


@contextlib.contextmanager
def temporary_directory(prefix):
    """As tempfile.TemporaryDirectory: yields the folder's name as a string."""
    name = tempfile.mkdtemp(prefix=prefix)
    try:
        yield name
    finally:
        remove_tree(name)
