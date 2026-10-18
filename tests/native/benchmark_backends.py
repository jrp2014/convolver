#!/usr/bin/env python3
"""Alternate backend execution order and retain all measurements as CSV."""
import argparse
import csv
import io
from pathlib import Path
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('fftw', type=Path)
parser.add_argument('accelerate', type=Path)
parser.add_argument('--seconds', type=float, default=.25)
parser.add_argument('--rounds', type=int, default=5)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.rounds < 3 or args.seconds <= 0:
    parser.error('use at least three rounds and positive duration')
rows = []
for repeat in range(args.rounds):
    for case in range(10):
        binaries = [args.fftw.resolve(), args.accelerate.resolve()]
        if (repeat+case) % 2:
            binaries.reverse()
        for binary in binaries:
            result = subprocess.run([str(binary), str(case), str(args.seconds)], capture_output=True, text=True, check=True)
            row = next(csv.DictReader(io.StringIO(result.stdout)))
            row['round'] = repeat+1
            rows.append(row)
with args.output.open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader(); writer.writerows(rows)
print('| Case | FFTW ns/frame | Accelerate ns/frame | FFTW/Accelerate |')
print('| --- | ---: | ---: | ---: |')
for case in dict.fromkeys(row['case'] for row in rows):
    values = {backend: statistics.median(float(row['ns_per_frame']) for row in rows
                                        if row['case']==case and row['backend']==backend)
              for backend in ('FFTW', 'Accelerate')}
    print(f"| {case} | {values['FFTW']:.1f} | {values['Accelerate']:.1f} | {values['FFTW']/values['Accelerate']:.2f}× |")
