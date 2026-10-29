#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Alternate backend execution order and retain all measurements as CSV."""

import argparse
import csv
import io
import statistics
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('fftw', type=Path, help='benchmark binary built with FFTW')
parser.add_argument('other', type=Path, help='benchmark binary built with another backend')
parser.add_argument('--seconds', type=float, default=0.25)
parser.add_argument('--rounds', type=int, default=5)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.rounds < 3 or args.seconds <= 0:
    parser.error('use at least three rounds and positive duration')
count = int(subprocess.run([str(args.fftw.resolve()), '--count'], capture_output=True, text=True, check=True).stdout)
rows = []
for repeat in range(args.rounds):
    for case in range(count):
        binaries = [args.fftw.resolve(), args.other.resolve()]
        if (repeat + case) % 2:
            binaries.reverse()
        for binary in binaries:
            result = subprocess.run(
                [str(binary), str(case), str(args.seconds)], capture_output=True, text=True, check=True
            )
            row = next(csv.DictReader(io.StringIO(result.stdout)))
            row['round'] = repeat + 1
            rows.append(row)
with args.output.open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
first, second = dict.fromkeys(row['backend'] for row in rows)
if second == 'FFTW':
    first, second = second, first
print(f'| Case | {first} ns/frame | {second} ns/frame | {first}/{second} |')
print('| --- | ---: | ---: | ---: |')
for case in dict.fromkeys(row['case'] for row in rows):
    values = {
        backend: statistics.median(
            float(row['ns_per_frame']) for row in rows if row['case'] == case and row['backend'] == backend
        )
        for backend in (first, second)
    }
    print(f'| {case} | {values[first]:.1f} | {values[second]:.1f} | {values[first] / values[second]:.2f}× |')
