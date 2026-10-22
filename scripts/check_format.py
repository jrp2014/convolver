#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check (default) or apply (--fix) clang-format 18 on the portable C++ sources.

Files are found by location, so a new adapter under plugins/ or a new test
folder is covered without editing a list. Historical code, vendored code and
the regression harnesses that slice historical source are excluded.

pre-commit runs this with --fix and a pinned clang-format (see
.pre-commit-config.yaml), so `pre-commit run` and the lint workflow own the
formatting check. Python rather than shell so the hook also runs on Windows.

Run from anywhere: python3 scripts/check_format.py [--fix]
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
MAJOR = '18'
PATHSPECS = (
    'include/*.hpp', 'include/*.h',
    'portable/*.hpp', 'portable/*.cpp', 'portable/*.h',
    'plugins/*.hpp', 'plugins/*.cpp', 'plugins/*.h', 'plugins/*.mm',
    'tests/*.hpp', 'tests/*.cpp', 'tests/*.h',
    ':!:portable/third_party/', ':!:tests/regression/',
)
INSTALL_HINT = """\
Install it with 'python3 -m pip install clang-format==18.1.8' (any OS),
'apt-get install clang-format-18' (Ubuntu) or 'brew install llvm@18' (macOS),
or run 'pre-commit run clang-format --all-files', which installs it for you.
Set CLANG_FORMAT to the binary if it is somewhere else."""


def major_version(binary):
    """Return the major version a clang-format binary reports, or None."""
    try:
        out = subprocess.run([binary, '--version'], capture_output=True, text=True,
                             check=False).stdout
    except OSError:
        return None
    match = re.search(r'clang-format version (\d+)\.', out)
    return match.group(1) if match else None


def find_clang_format():
    """Pick clang-format 18, or exit with advice.

    CLANG_FORMAT wins when set. Otherwise the copy installed next to this
    Python (pre-commit's pinned wheel) comes first, so the pin is what runs
    even when a system clang-format-18 of another patch release is on PATH.
    """
    explicit = os.environ.get('CLANG_FORMAT')
    if explicit:
        candidates = [explicit]
    else:
        here = Path(sys.executable).parent
        candidates = [str(here / name) for name in ('clang-format', 'clang-format.exe')
                      if (here / name).is_file()]
        candidates += [found for name in ('clang-format-18', 'clang-format')
                       if (found := shutil.which(name))]
    seen = []
    for binary in candidates:
        version = major_version(binary)
        if version == MAJOR:
            return binary
        seen.append(f'  {binary}: ' + (f'clang-format {version}' if version
                                         else 'could not be run, or is not clang-format'))
    if seen:
        sys.exit('clang-format 18 is needed; other major versions lay out the same code\n'
                 'differently, so CI would disagree. Found:\n' + '\n'.join(seen) + '\n'
                 + INSTALL_HINT)
    sys.exit('clang-format 18 was not found.\n' + INSTALL_HINT)


def sources():
    try:
        out = subprocess.run(
            ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--',
             *PATHSPECS], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        sys.exit(f'Could not list the C++ sources with git ({error}); run this from a Git '
                 'checkout with git on PATH.')
    return sorted({name for name in out.split('\0') if name})


def is_formatted(binary, name):
    result = subprocess.run([binary, '--dry-run', '--Werror', name], cwd=ROOT,
                            capture_output=True, check=False)
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--fix', action='store_true',
                        help='reformat files in place (exits 1 if anything changed)')
    args = parser.parse_args()

    binary = find_clang_format()
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

    if subprocess.run([binary, '--dry-run', '--Werror', *files], cwd=ROOT,
                      check=False).returncode != 0:
        print("clang-format: the files above are not formatted. Run "
              "'python3 scripts/check_format.py --fix'\n"
              "or 'pre-commit run --all-files' to reformat them, then commit the result.",
              file=sys.stderr)
        return 1
    print(f'clang-format: {len(files)} files formatted correctly')
    return 0


if __name__ == '__main__':
    sys.exit(main())
