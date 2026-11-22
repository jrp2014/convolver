# SPDX-License-Identifier: GPL-2.0-or-later
import csv
import random
import subprocess
import sys
from pathlib import Path

mode = sys.argv[1]
if mode not in ('warm', 'warm-long', 'legacy-style'):
    raise SystemExit('Expected warm, warm-long or legacy-style')
jobs = []
if mode in ('warm', 'warm-long'):
    for stem in ('Filter', '32bitFilterDRC'):
        for block in (256, 1024) if mode == 'warm' else (4096, 8192):
            for exe, kind, setting in [
                ('fftw', 'old', 65536 // block),
                ('fftw', 'new', block),
                ('accelerate', 'new', block),
            ]:
                jobs.append((stem, 2, 44100, 'stereo', exe, kind, setting))
        if mode == 'warm':
            jobs.append((stem, 2, 44100, 'stereo', 'accelerate', 'new', 0))
    # A low partition count favours throughput in the legacy engine; show its latency.
    if mode == 'warm':
        jobs.extend(('Filter', 2, 44100, 'stereo', 'fftw', 'old', setting) for setting in (0, 8))
else:
    # The published six-channel example ran with MEASURE; include that fixture and both stereo candidates.
    for stem, channels, rate, layout in [
        ('Filter', 2, 44100, 'stereo'),
        ('32bitFilterDRC', 2, 44100, 'stereo'),
        ('PerfectDiracDelta-48000-1-65535', 1, 48000, 'matrix'),
    ]:
        for exe, kind, setting in [('fftw', 'old', 8), ('fftw', 'new', 8192), ('accelerate', 'new', 8192)]:
            jobs.append((stem, channels, rate, layout, exe, kind, setting))
with Path(mode + '.csv').open('w', newline='') as f:
    w = csv.writer(f)
    w.writerow(
        [
            'round',
            'fixture',
            'ir_file_channels',
            'rate',
            'layout',
            'backend',
            'engine',
            'setting',
            'callback',
            'mode',
            'latency',
            'runs',
            'seconds',
            'realtime',
            'one_core_pct',
            'oracle_relative',
            'checksum',
        ]
    )
    for repeat in range(1, (6 if mode.startswith('warm') else 4)):
        order = jobs.copy()
        random.Random(10000 + repeat).shuffle(order)  # noqa: S311 - reproducible measurement order
        for stem, ch, rate, layout, exe, kind, setting in order:
            cb = 64 if mode.startswith('warm') else 1310720
            args = [
                './bench-' + exe,
                stem + '.f32',
                ch,
                rate,
                layout,
                kind,
                setting,
                cb,
                ('warm' if mode.startswith('warm') else mode),
                0.42 if mode.startswith('warm') else 0.2,
            ]
            row = subprocess.check_output(list(map(str, args)), text=True).strip().split(',')
            w.writerow([repeat, stem, ch, rate, layout, exe, kind, setting, cb, mode, *row])
            f.flush()
