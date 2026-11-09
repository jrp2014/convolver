#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the engine of two source revisions, each built separately (PERF-2).

jrp's review of c2edf84: a baseline must be its own build of a pinned revision,
never a copy kept in the current source, so that changing the engine cannot
silently change the baseline. This script checks out each revision into its own
worktree, builds it (Release, benchmarks on), refuses two revisions whose engine
source is the same, checks both against the direct double-precision references
of the first-use fixtures (the stereo cross-feed and the 2-to-6 crossover), and
then times convolver_callback_benchmark's warmed processing with production
routing in one engine (each row gives the number of timed callbacks behind
its percentiles; before 0.13.0 the benchmark counted preparation and warm-up
in --seconds, so a slow baseline case can have as few as 64):

  stereo     2 in, 2 out, two paths (diagonal)
  true       2 in, 2 out, four paths (matrix: true stereo, one engine)
  crossover  2 in, 6 out, six paths

Each row records the revision, its commit, the build type, the FFT backend and
kernels the benchmark reports, and the CPU. The builds take turns case by case,
and every other round reverses their order, so drift on the machine (heat,
background load) affects both alike.

  python3 tests/native/benchmarks/compare_engines.py --baseline bf6a207 \\
      --candidate HEAD --work ../engine-compare > engines.csv

The baseline runs at its own Latency (--baseline-latency, default 256, the
plug-ins' default before PERF-2); the candidate at each --latency. Extra CMake
options (for example -DCONVOLVER_FFT_BACKEND=OOURA) go after --cmake.
"""

import argparse
import csv
import io
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / 'tests' / 'plugins'))
from fixtures import TOLERANCE, read_float_wav, worst_error  # noqa: E402

ENGINE_SOURCES = ('portable/engine.cpp',)
LAYOUTS = {'stereo': ('diagonal', 2, 2), 'true': ('matrix', 2, 2), 'crossover': ('crossover', 2, 6)}
FIXTURES = ('stereo', 'crossover')


def git(*args, cwd=REPOSITORY):
    return subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def cpu_name():
    try:
        for line in Path('/proc/cpuinfo').read_text().splitlines():
            if line.startswith('model name'):
                return line.split(':', 1)[1].strip()
    except OSError:
        pass
    if sys.platform == 'darwin':
        return subprocess.run(
            ['sysctl', '-n', 'machdep.cpu.brand_string'], capture_output=True, text=True, check=False
        ).stdout.strip()
    return platform.processor() or platform.machine()


class Build:
    def __init__(self, label, revision, work, cmake_options, jobs):
        self.label = label
        self.commit = git('rev-parse', '--verify', f'{revision}^{{commit}}')
        self.source = work / f'{label}-{self.commit[:12]}'
        self.binary = self.source / 'build'
        if not self.source.exists():
            git('worktree', 'add', '--detach', str(self.source), self.commit)
        subprocess.run(
            [
                'cmake',
                '-S',
                str(self.source),
                '-B',
                str(self.binary),
                '-DCMAKE_BUILD_TYPE=Release',
                '-DCONVOLVER_BUILD_BENCHMARKS=ON',
                *cmake_options,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        subprocess.run(
            [
                'cmake',
                '--build',
                str(self.binary),
                '--parallel',
                str(jobs),
                '--target',
                'convolver_cli',
                'convolver_callback_benchmark',
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        self.cli = self.executable('convolver')
        self.benchmark = self.executable('convolver_callback_benchmark')

    def executable(self, name):
        for candidate in (self.binary / name, self.binary / f'{name}.exe', self.binary / 'Release' / f'{name}.exe'):
            if candidate.exists():
                return candidate
        raise FileNotFoundError(f'{self.label}: no {name} in {self.binary}')

    def check(self):
        """Render each first-use fixture with default options and compare it
        with its direct double-precision reference."""
        worst = 0.0
        for name in FIXTURES:
            fixture = REPOSITORY / 'examples' / 'first-use' / name
            with tempfile.TemporaryDirectory() as directory:
                out = Path(directory) / 'out.wav'
                subprocess.run(
                    [
                        str(self.cli),
                        '--filter',
                        str(fixture / 'filter.cfg'),
                        '--input',
                        str(fixture / 'input.wav'),
                        '--output',
                        str(out),
                    ],
                    check=True,
                    capture_output=True,
                )
                error = worst_error(read_float_wav(out), read_float_wav(fixture / 'expected.wav'))
            if error >= TOLERANCE:
                raise AssertionError(f'{self.label} {name}: worst error {error} against the reference')
            worst = max(worst, error)
        return worst

    def measure(self, layout, taps, callback, latency, seconds):
        kind, inputs, outputs = LAYOUTS[layout]
        result = subprocess.run(
            [
                str(self.benchmark),
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
                str(seconds),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        lines = [line for line in result.stdout.splitlines() if line.strip()]
        row = next(csv.DictReader(io.StringIO('\n'.join(lines[-2:]))))
        # The number of timed callbacks behind the percentiles. Builds older
        # than the column time warm callbacks in batches of 64.
        if not row.get('callbacks'):
            row['callbacks'] = str(int(row['repetitions']) * 64)
        return row


def numbers(text):
    return [int(value) for value in text.split(',') if value]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--baseline', required=True, help='revision of the baseline engine')
    parser.add_argument('--candidate', default='HEAD', help='revision of the candidate engine')
    parser.add_argument('--work', type=Path, required=True, help='folder for the worktrees and builds')
    parser.add_argument('--baseline-latency', type=int, default=256)
    parser.add_argument('--latency', default='0,256', help='candidate latencies, comma separated')
    parser.add_argument('--taps', default='4096,16384,65536,480000,1440000')
    parser.add_argument('--callbacks', default='64,512')
    parser.add_argument('--layouts', default='stereo,true,crossover')
    parser.add_argument('--seconds', type=float, default=1.0, help='per run')
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    parser.add_argument('--cmake', nargs=argparse.REMAINDER, default=[], help='extra CMake options')
    o = parser.parse_args()
    o.work.mkdir(parents=True, exist_ok=True)
    builds = [
        Build('baseline', o.baseline, o.work.resolve(), o.cmake, o.jobs),
        Build('candidate', o.candidate, o.work.resolve(), o.cmake, o.jobs),
    ]
    if builds[0].commit == builds[1].commit:
        sys.exit('the baseline and the candidate are the same commit')
    if all(
        git('rev-parse', f'{builds[0].commit}:{path}') == git('rev-parse', f'{builds[1].commit}:{path}')
        for path in ENGINE_SOURCES
    ):
        sys.exit('the two revisions have the same engine source: nothing to compare')
    for build in builds:
        print(f'{build.label} {build.commit}: worst fixture error {build.check():.3g}', file=sys.stderr)
    cpu = cpu_name()
    fields = [
        'round',
        'build',
        'commit',
        'build_type',
        'cpu',
        'layout',
        'latency',
        'backend',
        'kernels',
        'taps',
        'callback',
        'paths',
        'ns_per_frame',
        'p50_us',
        'p99_us',
        'p999_us',
        'max_us',
        'callbacks',
    ]
    writer = csv.DictWriter(sys.stdout, fieldnames=fields, extrasaction='ignore')
    writer.writeheader()
    for r in range(1, o.rounds + 1):
        for layout in o.layouts.split(','):
            for taps in numbers(o.taps):
                for callback in numbers(o.callbacks):
                    # Odd rounds run the baseline first and even rounds the
                    # candidate, so neither always meets a cooler or quieter
                    # machine.
                    for build in builds if r % 2 else reversed(builds):
                        latencies = [o.baseline_latency] if build.label == 'baseline' else numbers(o.latency)
                        for latency in latencies:
                            row = build.measure(layout, taps, callback, latency, o.seconds)
                            row.update(
                                round=r,
                                build=build.label,
                                commit=build.commit[:12],
                                build_type='Release',
                                cpu=cpu,
                                layout=layout,
                                latency=latency,
                            )
                            writer.writerow(row)
                            sys.stdout.flush()


if __name__ == '__main__':
    main()
