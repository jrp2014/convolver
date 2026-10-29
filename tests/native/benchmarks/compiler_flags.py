#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build convolver_benchmark with several compiler flag sets and time them.

Each variant is a separate Release build of the core and benchmark only, with
CMAKE_CXX_FLAGS_RELEASE replaced by the variant's flags. Rounds alternate the
variants in a shuffled order per case, and every measurement is kept in the CSV.
Default cases are the room-correction workloads (10-18). Example, from the
repository root (about 30 minutes for the default variants and five rounds):

  python3 tests/native/benchmarks/compiler_flags.py --compiler clang++ --backend OOURA \\
      --work /tmp/flags --output flags.csv

Pin the benchmark to one core where the system allows it (--cpu 2 uses taskset
on Linux). Nothing else should run meanwhile. Add a variant with
--variant 'NAME=FLAGS', followed by |lto for link-time optimisation,
|baseline to build without the AVX2 kernels and |source=DIR to build another
source tree (an earlier commit from `git archive`, say), for example
--variant 'O3-baseline=-O3 -DNDEBUG|baseline'.
"""

import argparse
import csv
import io
import random
import shutil
import statistics
import subprocess
from pathlib import Path

DEFAULT_VARIANTS = [
    'O2=-O2 -DNDEBUG',
    'O3=-O3 -DNDEBUG',
    'O3-lto=-O3 -DNDEBUG|lto',
    'O3-v3=-O3 -march=x86-64-v3 -DNDEBUG',
    'O3-v3-nofma=-O3 -march=x86-64-v3 -ffp-contract=off -DNDEBUG',
]

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[3])
parser.add_argument('--compiler', default='c++')
parser.add_argument('--backend', default='OOURA', choices=['FFTW', 'OOURA', 'ACCELERATE'])
parser.add_argument(
    '--variant', action='append', help='NAME=FLAGS[|lto][|baseline][|source=DIR]; replaces the defaults'
)
parser.add_argument('--no-avx2-kernels', action='store_true', help='configure with CONVOLVER_AVX2_KERNELS=OFF')
parser.add_argument('--cases', default='10,11,12,13,14,15,16,17,18')
parser.add_argument('--rounds', type=int, default=5)
parser.add_argument('--seconds', type=float, default=0.3)
parser.add_argument('--cpu', help='CPU to pin the benchmark to with taskset (Linux)')
parser.add_argument('--work', type=Path, required=True, help='folder for the build directories')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.rounds < 3 or args.seconds <= 0:
    parser.error('use at least three rounds and a positive duration')

binaries = {}
for spec in args.variant or DEFAULT_VARIANTS:
    name, _, rest = spec.partition('=')
    flags, *extras = rest.split('|')
    source = args.source
    for extra in [e for e in extras if e.startswith('source=')]:
        source = Path(extra.partition('=')[2]).resolve()
        extras.remove(extra)
    unknown = set(extras) - {'lto', 'baseline'}
    if unknown:
        parser.error(f'unknown variant option {min(unknown)} in {spec}')
    avx2 = not (args.no_avx2_kernels or 'baseline' in extras)
    build = args.work / name
    subprocess.run(
        [
            'cmake',
            '-S',
            str(source),
            '-B',
            str(build),
            '-DCMAKE_BUILD_TYPE=Release',
            '-DCMAKE_CXX_COMPILER=' + args.compiler,
            '-DCMAKE_CXX_FLAGS_RELEASE=' + flags,
            '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=' + ('ON' if 'lto' in extras else 'OFF'),
            '-DCONVOLVER_FFT_BACKEND=' + args.backend,
            '-DCONVOLVER_BUILD_IO=OFF',
            '-DCONVOLVER_BUILD_CLI=OFF',
            '-DBUILD_TESTING=OFF',
            '-DCONVOLVER_BUILD_BENCHMARKS=ON',
            '-DCONVOLVER_LEGACY_TESTS=OFF',
            '-DCONVOLVER_DOCUMENTATION_TESTS=OFF',
            '-DCONVOLVER_AVX2_KERNELS=' + ('ON' if avx2 else 'OFF'),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        ['cmake', '--build', str(build), '--target', 'convolver_benchmark', '--config', 'Release'],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    found = [p for p in build.rglob('convolver_benchmark*') if p.is_file() and p.suffix in ('', '.exe')]
    binaries[name] = found[0]

pin = ['taskset', '-c', args.cpu] if args.cpu and shutil.which('taskset') else []
cases = [int(c) for c in args.cases.split(',')]
order = random.Random(1)
rows = []
for repeat in range(args.rounds):
    for case in cases:
        names = list(binaries)
        order.shuffle(names)
        for name in names:
            result = subprocess.run(
                [*pin, str(binaries[name]), str(case), str(args.seconds)], capture_output=True, text=True, check=True
            )
            row = next(csv.DictReader(io.StringIO(result.stdout)))
            row.update(variant=name, compiler=args.compiler, round=repeat + 1)
            rows.append(row)
with args.output.open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)

base = 'O3' if 'O3' in binaries else next(iter(binaries))
print(f'Median ns/frame, and speedup over {base} (above 1 is faster)')
print('| Case | ' + ' | '.join(binaries) + ' |')
print('| --- |' + ' ---: |' * len(binaries))
for case in dict.fromkeys(row['case'] for row in rows):
    median = {
        name: statistics.median(
            float(row['ns_per_frame']) for row in rows if row['case'] == case and row['variant'] == name
        )
        for name in binaries
    }
    print(f'| {case} | ' + ' | '.join(f'{median[n]:.0f} ({median[base] / median[n]:.2f}×)' for n in binaries) + ' |')
