# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the raw timings and print medians and ranges for each measured configuration."""

import csv
import itertools
import math
import statistics
from pathlib import Path


def configuration(row):
    return row['fixture'], int(row['latency']), row['backend'], row['engine']


def filter_and_delay(row):
    return row['fixture'], int(row['latency'])


def check(condition, location):
    if not condition:
        raise ValueError(f'Invalid benchmark results: {location}')


for name, count, rounds in [('warm', 80, 5), ('warm-long', 60, 5), ('legacy-style', 27, 3), ('archive-sweep', 18, 2)]:
    with Path(name + '.csv').open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    check(len(rows) == count, (name, len(rows)))
    for row in rows:
        check(0 <= float(row['oracle_relative']) < 5e-5, (name, 'oracle error', row))
        check(float(row['realtime']) > 0 and int(row['runs']) >= 1, (name, 'timing', row))
        check(math.isfinite(float(row['checksum'])), (name, 'checksum', row))
        check(abs(float(row['realtime']) * float(row['one_core_pct']) - 100) < 1e-5, (name, 'one core', row))
    print(name, 'rows', count, 'worst relative error', max(float(row['oracle_relative']) for row in rows))
    if name == 'archive-sweep':
        for parts, group in itertools.groupby(rows, lambda row: row['parts']):
            entries = list(group)
            check([int(row['iteration']) for row in entries] == [1, 2], (name, parts, 'iterations'))
            check(all(int(row['runs']) == 1 for row in entries), (name, parts, 'runs'))
            print(
                '  partitions',
                parts,
                'latency',
                entries[0]['latency'],
                'x realtime',
                *[round(float(row['realtime']), 2) for row in entries],
            )
        continue
    for settings, group in itertools.groupby(sorted(rows, key=configuration), configuration):
        entries = list(group)
        check(sorted(int(row['round']) for row in entries) == list(range(1, rounds + 1)), (name, settings, 'rounds'))
        speed = [float(row['realtime']) for row in entries]
        print(' ', *settings, 'x realtime:', *[round(f(speed), 2) for f in (min, statistics.median, max)])
    for settings, group in itertools.groupby(sorted(rows, key=filter_and_delay), filter_and_delay):
        values = [float(row['checksum']) / int(row['runs']) for row in group]
        check(max(values) - min(values) < 2e-5 * max(1, *map(abs, values)), (name, settings, 'noise output'))
