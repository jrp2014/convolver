# SPDX-License-Identifier: GPL-2.0-or-later
"""Alternate baseline/vDSP in one Release binary; retain every measurement."""
import argparse
import csv
import io
import pathlib
import random
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
parser.add_argument('--repeats', type=int, default=7)
parser.add_argument('--seconds', type=float, default=0.4)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
for kind, executable, cases in [
    ('engine', 'convolver_benchmark', [(str(i),) for i in range(19)]),
    ('kernel', 'convolver_mac_benchmark', [('65', '4'), ('129', '128'),
        ('257', '16'), ('257', '256'), ('257', '512'), ('1025', '64'), ('257', '2048')]),
]:
    with (args.output / (kind + '.csv')).open('w', newline='') as output:
        writer = None
        for repeat in range(args.repeats):
            order = cases.copy()
            random.Random(100 + repeat).shuffle(order)
            for case in order:
                variants = ['baseline', 'vDSP'] if repeat % 2 == 0 else ['vDSP', 'baseline']
                for position, variant in enumerate(variants):
                    command = [str(args.build / executable), *case, str(args.seconds)]
                    if variant == 'baseline':
                        command.append('--baseline')
                    completed = subprocess.run(command, check=True, capture_output=True, text=True)
                    row = next(csv.DictReader(io.StringIO(completed.stdout)))
                    if row['kernels'] != variant:
                        raise RuntimeError('benchmark selected wrong kernel: ' + completed.stdout)
                    row = {'repeat': repeat, 'pair_position': position, **row}
                    if writer is None:
                        writer = csv.DictWriter(output, fieldnames=list(row), lineterminator="\n")
                        writer.writeheader()
                    writer.writerow(row)
                    output.flush()
            print(f'{kind}: completed repeat {repeat + 1}/{args.repeats}', flush=True)
