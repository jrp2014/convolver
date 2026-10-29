#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Cloud bootstrap keeps the commit hook when optional Clang downloads fail."""

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name != 'nt' and shutil.which('bash'), 'cloud setup is Linux Bash')
class CloudSetupTests(unittest.TestCase):
    def test_failed_clang_upgrade_still_installs_available_commit_hook(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            venv = root / 'lint'
            binary = venv / 'bin'
            binary.mkdir(parents=True)
            marker = root / 'hook-installed'
            for name, body in (
                ('python', '#!/bin/sh\nexit 1\n'),
                (
                    'pre-commit',
                    f'#!/bin/sh\nif [ "$1" = install ]; then\n  touch {shlex.quote(str(marker))}\nfi\nexit 0\n',
                ),
            ):
                path = binary / name
                path.write_text(body)
                path.chmod(0o755)
            source = (ROOT / '.claude/hooks/session-start.sh').read_text()
            # Exercise the real hook-install function with an isolated tool prefix.
            source = source[source.index('install_lint_hook() {') : source.index('# Neither step')]
            original = 'local venv="${HOME}/.cache/convolver-lint"'
            self.assertEqual(source.count(original), 1)
            source = source.replace(original, 'local venv=' + shlex.quote(str(venv)))
            script = root / '.claude/hooks/session-start.sh'
            script.parent.mkdir(parents=True)
            script.write_text('set -euo pipefail\n' + source + '\ninstall_lint_hook || true\n')
            result = subprocess.run(['bash', str(script)], capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Could not update the Clang tools', result.stderr)
            self.assertTrue(marker.exists(), 'Clang download failure prevented Git-hook setup')


if __name__ == '__main__':
    unittest.main()
