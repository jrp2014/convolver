# SPDX-License-Identifier: GPL-2.0-or-later
import csv
import subprocess
from pathlib import Path

with Path('archive-sweep.csv').open('w', newline='') as f:
    w = csv.writer(f)
    w.writerow(
        ['iteration', 'parts', 'latency', 'runs', 'seconds', 'realtime', 'one_core_pct', 'oracle_relative', 'checksum']
    )
    for parts in range(9):
        for repeat in (1, 2):
            # A single SecCalc measurement per entry, as in perftest 8 2 1 benchmark.txt.
            args = [
                './bench-fftw',
                'PerfectDiracDelta-48000-1-65535.f32',
                '1',
                '48000',
                'matrix',
                'old',
                str(parts),
                '1310720',
                'legacy-style',
                '0',
            ]
            vals = subprocess.check_output(args, text=True).strip().split(',')
            w.writerow([repeat, parts, *vals])
            f.flush()
