#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check (default) or apply (--fix) the latest stable clang-format on the portable C++ sources.

Files are found by location, so a new adapter under plugins/ or a new test
folder is covered without editing a list. Historical code, vendored code and
the regression harnesses that slice historical source are excluded.

pre-commit runs this with --fix and the latest stable clang-format package (see
.pre-commit-config.yaml), so `pre-commit run` and the lint workflow own the
formatting check. Python rather than shell so the hook also runs on Windows.

Run from anywhere: python3 scripts/check_format.py [--fix]
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATHSPECS = (
    'include/*.hpp',
    'include/*.h',
    'portable/*.hpp',
    'portable/*.cpp',
    'portable/*.h',
    'plugins/*.hpp',
    'plugins/*.cpp',
    'plugins/*.h',
    'plugins/*.mm',
    'tests/*.hpp',
    'tests/*.cpp',
    'tests/*.h',
    'tests/*.mm',
    ':!:portable/third_party/',
    ':!:tests/regression/',
)
INSTALL_HINT = """\
Install the latest stable package with 'python3 -m pip install --upgrade clang-format',
or run 'pre-commit clean' then 'pre-commit run clang-format --all-files'.
Set CLANG_FORMAT to the binary if it is somewhere else."""


def major_version(binary):
    """Return the major version of a stable clang-format binary, or None."""
    try:
        out = subprocess.run([binary, '--version'], capture_output=True, text=True, check=False).stdout
    except OSError:
        return None
    match = re.search(r'clang-format version (\d+)\.\d+\.\d+(?=\s|$)', out)
    return match.group(1) if match else None


def find_clang_format():
    """Use an explicit stable tool or the hook environment's installed release.

    Tool installation owns upgrades. The hook installs the latest stable package;
    CI keys its environment cache by that release. Discovery needs no network.
    """
    explicit = os.environ.get('CLANG_FORMAT')
    if explicit:
        candidates = [explicit]
    else:
        here = Path(sys.executable).parent
        candidates = [str(here / name) for name in ('clang-format', 'clang-format.exe') if (here / name).is_file()]
        candidates += [found for name in ('clang-format',) if (found := shutil.which(name))]
    seen = []
    for binary in candidates:
        version = major_version(binary)
        if version is not None:
            return binary
        seen.append(
            f'  {binary}: ' + (f'clang-format {version}' if version else 'could not be run, or is not clang-format')
        )
    if seen:
        sys.exit('A stable clang-format release is needed. Found:\n' + '\n'.join(seen) + '\n' + INSTALL_HINT)
    sys.exit('A stable clang-format was not found.\n' + INSTALL_HINT)


def sources():
    try:
        out = subprocess.run(
            ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', *PATHSPECS],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        sys.exit(f'Could not list the C++ sources with git ({error}); run this from a Git checkout with git on PATH.')
    return sorted({name for name in out.split('\0') if name})


def is_formatted(binary, name):
    result = subprocess.run([binary, '--dry-run', '--Werror', name], cwd=ROOT, capture_output=True, check=False)
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--fix', action='store_true', help='reformat files in place (exits 1 if anything changed)')
    args = parser.parse_args()

    binary = find_clang_format()
    subprocess.run([binary, '--version'], check=True)
    files = sources()
    if not files:
        sys.exit('No C++ sources found')

    if args.fix:
        changed = [name for name in files if not is_formatted(binary, name)]
        if not changed:
            print(f'clang-format: {len(files)} files already formatted')
            return 0
        subprocess.run([binary, '-i', *changed], cwd=ROOT, check=True)
        print(f'clang-format: reformatted {len(changed)} of {len(files)} files:')
        for name in changed:
            print(f'  {name}')
        print('Review the changes, then stage and commit them.')
        return 1

    if subprocess.run([binary, '--dry-run', '--Werror', *files], cwd=ROOT, check=False).returncode != 0:
        print(
            'clang-format: the files above are not formatted. Run '
            "'python3 scripts/check_format.py --fix'\n"
            "or 'pre-commit run --all-files' to reformat them, then commit the result.",
            file=sys.stderr,
        )
        return 1
    print(f'clang-format: {len(files)} files formatted correctly')
    return 0


if __name__ == '__main__':
    sys.exit(main())
