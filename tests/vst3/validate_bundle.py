# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the final resource seal before asking the SDK to load a bundle.

Usage: validate_bundle.py VALIDATOR [VALIDATOR_OPTIONS...] BUNDLE
"""

import platform
import re
import subprocess
import sys

validator, options, bundle = sys.argv[1], sys.argv[2:-1], sys.argv[-1]
if platform.system() == 'Darwin':
    subprocess.run(['codesign', '--verify', '--deep', '--strict', bundle], check=True)
result = subprocess.run(
    [validator, *options, bundle], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False
)
print(result.stdout)
if result.returncode or not re.search(r'\b[1-9]\d* tests passed, 0 tests failed', result.stdout):
    raise SystemExit('SDK validator did not report a successful nonempty test run')
