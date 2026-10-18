# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the final resource seal before asking the SDK to load a bundle."""
import platform
import re
import subprocess
import sys

if platform.system() == 'Darwin':
    subprocess.run(['codesign', '--verify', '--deep', '--strict', sys.argv[2]], check=True)
result = subprocess.run([sys.argv[1], sys.argv[2]], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(result.stdout)
if result.returncode or not re.search(r'\b[1-9]\d* tests passed, 0 tests failed', result.stdout):
    raise SystemExit('SDK validator did not report a successful nonempty test run')
