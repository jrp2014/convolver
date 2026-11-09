# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare prepared FFT builds; see the adjacent dated records for setup."""

import argparse
import csv
import datetime as dt
import itertools
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', type=Path)
parser.add_argument('--ooura', action='store_true', help='Compare Ooura with a fresh Accelerate control')
parser.add_argument('--pffft-build', type=Path, help='Compare an experimental PFFFT build with Ooura')
options = parser.parse_args()
if options.ooura and options.pffft_build:
    parser.error('--ooura and --pffft-build are separate comparisons')
root = options.root.resolve()
prefix = 'pffft-' if options.pffft_build else ('ooura-' if options.ooura else '')
source = root / 'source'
sys.path.insert(0, str(source / 'tests/plugins'))
from fixtures import TOLERANCE, read_float_wav, worst_error  # noqa: E402

modes = (
    ('ooura', 'pffft')
    if options.pffft_build
    else (('accelerate', 'ooura') if options.ooura else ('accelerate', 'fftw_estimate', 'fftw_patient_wisdom'))
)
layouts = {'stereo': ('diagonal', 2, 2), 'true': ('matrix', 2, 2), 'crossover': ('crossover', 2, 6)}
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()


def build(mode):
    if mode == 'pffft':
        return options.pffft_build.resolve()
    return root / (mode if mode in {'accelerate', 'ooura'} else 'fftw')


def environment(mode):
    env = os.environ.copy()
    env.pop('CONVOLVER_BENCH_WISDOM', None)
    if mode == 'fftw_patient_wisdom':
        env['CONVOLVER_BENCH_WISDOM'] = str(root / 'patient.fftw')
    return env


errors = []
for mode, latency, fixture in itertools.product(modes, (0, 256), ('stereo', 'crossover')):
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / 'out.wav'
        f = source / 'examples/first-use' / fixture
        cmd = [
            str(build(mode) / 'convolver/convolver'),
            '--filter',
            str(f / 'filter.cfg'),
            '--input',
            str(f / 'input.wav'),
            '--output',
            str(out),
            '--block-size',
            str(latency),
        ]
        if mode == 'fftw_patient_wisdom':
            cmd += ['--fft-wisdom', str(root / 'patient.fftw')]
        subprocess.run(cmd, check=True, capture_output=True)
        error = worst_error(read_float_wav(out), read_float_wav(f / 'expected.wav'))
        if error >= TOLERANCE:
            raise ValueError(f'{mode}, latency {latency}, {fixture}: error {error}')
        errors.append({'mode': mode, 'latency': latency, 'fixture': fixture, 'error': error})
(root / f'evidence/{prefix}fixture-errors.json').write_text(json.dumps(errors, indent=2) + '\n')
print(f'All {len(errors)} fixture comparisons passed; worst error', max(x['error'] for x in errors), flush=True)

orders = list(itertools.permutations(modes)) * (2 if len(modes) == 2 else 1)
total = len(orders) * len(layouts) * 3 * 2 * 2 * len(modes)
started = time.monotonic()
with (root / f'{prefix}backends-mac.csv').open('w') as f:
    writer = None
    count = 0
    for round_number, order in enumerate(orders, 1):
        for layout, taps, callback, latency in itertools.product(layouts, (4096, 65536, 1440000), (64, 512), (0, 256)):
            kind, inputs, outputs = layouts[layout]
            for mode in order:
                cmd = [
                    str(build(mode) / 'convolver_backend_benchmark'),
                    '--scenario',
                    'warm',
                    '--layout',
                    kind,
                    '--inputs',
                    str(inputs),
                    '--outputs',
                    str(outputs),
                    '--taps',
                    str(taps),
                    '--callback',
                    str(callback),
                    '--block',
                    str(latency),
                    '--seconds',
                    '1',
                ]
                p = subprocess.run(cmd, check=True, capture_output=True, text=True, env=environment(mode))
                row = dict(next(csv.DictReader(p.stdout.strip().splitlines()[-2:])))
                row = dict(
                    round=round_number,
                    mode=mode,
                    layout=layout,
                    commit=commit,
                    recorded_at=dt.datetime.now().astimezone().isoformat(),
                    **row,
                )
                if writer is None:
                    writer = csv.DictWriter(f, fieldnames=list(row))
                    writer.writeheader()
                writer.writerow(row)
                f.flush()
                count += 1
            if count % 36 == 0:
                print(f'{count}/{total} rows, elapsed {time.monotonic() - started:.1f} s', flush=True)
print('Processing comparison complete', flush=True)
with (root / f'{prefix}preparation-mac.csv').open('w') as f:
    writer = None
    for r, order in enumerate(orders, 1):
        for taps, latency in itertools.product((4096, 1440000), (0, 256)):
            for mode in order:
                cmd = [
                    str(build(mode) / 'convolver_backend_benchmark'),
                    '--scenario',
                    'startup',
                    '--layout',
                    'diagonal',
                    '--inputs',
                    '2',
                    '--outputs',
                    '2',
                    '--taps',
                    str(taps),
                    '--callback',
                    '64',
                    '--block',
                    str(latency),
                    '--seconds',
                    '0.2',
                ]
                p = subprocess.run(cmd, check=True, capture_output=True, text=True, env=environment(mode))
                row = dict(next(csv.DictReader(p.stdout.strip().splitlines()[-2:])))
                row = dict(round=r, mode=mode, **row)
                if writer is None:
                    writer = csv.DictWriter(f, fieldnames=list(row))
                    writer.writeheader()
                writer.writerow(row)
                f.flush()
print('Preparation comparison complete', flush=True)
