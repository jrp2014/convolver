#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run clang-tidy on several translation units at once.

Each file gets its own `clang-tidy -p BUILD FILE`, as many at a time as
--jobs allows (default: one per CPU). Every file's output is printed whole, in
the order given, and the files that failed are listed at the end; the exit
status is 1 if any failed. The checks are those of .clang-tidy, unchanged.

    python3 scripts/clang_tidy.py -p build portable/engine.cpp portable/host.cpp
"""

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import time


def analyse(executable, build, source):
    started = time.monotonic()
    result = subprocess.run(
        [executable, '-p', build, source], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False
    )
    return result.returncode, result.stdout.decode('utf-8', errors='replace'), time.monotonic() - started


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('-p', dest='build', required=True, help='build directory with compile_commands.json')
    parser.add_argument(
        '--jobs', type=int, default=os.cpu_count() or 2, help='files analysed at once (default: the number of CPUs)'
    )
    parser.add_argument(
        '--clang-tidy',
        default=os.environ.get('CLANG_TIDY', 'clang-tidy'),
        help='clang-tidy executable (default: $CLANG_TIDY, else clang-tidy on PATH)',
    )
    parser.add_argument('sources', nargs='+')
    args = parser.parse_args(argv)
    executable = shutil.which(args.clang_tidy)
    if executable is None:
        print(f'clang_tidy.py: cannot find {args.clang_tidy}; install it as in CONTRIBUTING.md', file=sys.stderr)
        return 2
    jobs = max(1, min(args.jobs, len(args.sources)))
    print(f'clang-tidy: {len(args.sources)} files, {jobs} at a time', flush=True)
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        runs = [pool.submit(analyse, executable, args.build, source) for source in args.sources]
        for source, run in zip(args.sources, runs):
            status, output, seconds = run.result()
            outcome = 'passed' if status == 0 else f'FAILED (exit status {status})'
            print(f'--- {source}: {outcome}, {seconds:.1f} s', flush=True)
            if output:
                print(output, end='' if output.endswith('\n') else '\n', flush=True)
            if status != 0:
                failed.append(source)
    if failed:
        print(f'clang-tidy failed on {len(failed)} of {len(args.sources)} files: {" ".join(failed)}', flush=True)
        return 1
    print(f'clang-tidy passed on all {len(args.sources)} files', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
