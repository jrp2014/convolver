#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Collect what a failed CI test run leaves behind, for upload as an artifact.

DIAG-1 (jrp's review of 24 October 2026): native and adapter failures keep
their evidence, as the distribution and fuzz workflows already do. For each
build folder given, this copies CTest's full output (Testing/Temporary/
LastTest*.log, which holds every test's output, sanitizer reports and the
lifecycle tests' failing operation sequences included), the JUnit report
(junit.xml, written by ctest --output-junit), the Convolver logs the plug-in
tests write (convolver-tests.log) and CMakeCache.txt (compiler, flags and
options). identity.txt records the commit, runner image and compiler.

Usage: ci_evidence.py OUTPUT BUILD [BUILD ...]. Missing folders and files are
skipped, so the step can run whichever stage failed.
"""

import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def run(command):
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        return f'({" ".join(command)}: {error})'
    return (result.stdout or result.stderr).strip()


def cache_value(cache, name):
    for line in cache.read_text(errors='replace').splitlines():
        if line.startswith(name + ':'):
            return line.split('=', 1)[1]
    return ''


def collect(build, out):
    copied = []
    patterns = ['Testing/Temporary/LastTest*.log', 'junit.xml', 'CMakeCache.txt', '**/convolver-tests.log']
    for pattern in patterns:
        for found in sorted(build.glob(pattern)):
            if not found.is_file():
                continue
            target = out / found.relative_to(build)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(found, target)
            copied.append(str(found.relative_to(build)))
    return copied


def main(argv):
    if len(argv) < 3:
        print('usage: ci_evidence.py OUTPUT BUILD [BUILD ...]', file=sys.stderr)
        return 2
    output = Path(argv[1])
    output.mkdir(parents=True, exist_ok=True)
    identity = [
        f'commit: {run(["git", "rev-parse", "HEAD"])}',
        (
            f'workflow: {os.environ.get("GITHUB_WORKFLOW", "")} / job {os.environ.get("GITHUB_JOB", "")}'
            f' / run {os.environ.get("GITHUB_RUN_ID", "")} attempt {os.environ.get("GITHUB_RUN_ATTEMPT", "")}'
        ),
        (
            f'runner: {os.environ.get("RUNNER_OS", "")} {os.environ.get("RUNNER_ARCH", "")}, image '
            # GitHub's runner image variables are named in mixed case.
            f'{os.environ.get("ImageOS", "")} {os.environ.get("ImageVersion", "")}'  # noqa: SIM112
        ),
        f'platform: {platform.platform()} ({platform.machine()})',
    ]
    for name in argv[2:]:
        build = Path(name)
        if not build.is_dir():
            identity.append(f'{name}: not built')
            continue
        copied = collect(build, output / build.name)
        cache = build / 'CMakeCache.txt'
        if cache.is_file():
            compiler = cache_value(cache, 'CMAKE_CXX_COMPILER')
            identity.append(f'{name}: build type {cache_value(cache, "CMAKE_BUILD_TYPE")}, compiler {compiler}')
            if compiler:
                version = run([compiler, '--version'])
                identity.append('  ' + (version.splitlines()[0] if version else '(no version)'))
        identity.append(f'  kept: {", ".join(copied) if copied else "nothing"}')
    (output / 'identity.txt').write_text('\n'.join(identity) + '\n')
    print('\n'.join(identity))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
