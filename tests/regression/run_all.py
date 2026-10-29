#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the portable tests of the canonical historical/ tree. No extra Python packages."""

from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
scripts = (
    'core_regressions.py',
    'host_regressions.py',
    'gain_regressions.py',
    'branch_regressions.py',
    'historical_diagnostics.py',
    'historical_io_regressions.py',
    'pipe_regressions.py',
    'pipe_dsp_regressions.py',
    'documentation_regressions.py',
)
failed = []
for script in scripts:
    print('\nRunning ' + script, flush=True)
    result = subprocess.run([sys.executable, str(HERE / script)])
    if result.returncode:
        failed.append(script)
if failed:
    print('FAILED: ' + ', '.join(failed))
    raise SystemExit(1)
print('\nPASS: all %d portable regression suites' % len(scripts))
