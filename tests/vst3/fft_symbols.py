# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that a plug-in binary cannot share an FFTW planner with its host.

Usage: fft_symbols.py BACKEND PLUGIN_BINARY

A plug-in must never export FFTW symbols: a host or another plug-in could bind
to them. Unless the build deliberately chose FFTW, it must not import FFTW
either. Exit code 77 means the platform's symbol tool was not found (skipped).
"""

import platform
import re
import shutil
import subprocess
import sys

FFTW = re.compile(r'\b_?fftwf?_\w+')


def run(command):
    return subprocess.run(
        command, check=True, text=True, errors='replace', stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    ).stdout


def symbols(binary):
    """Return (exported, imported, libraries) FFTW-related names."""
    system = platform.system()
    if system == 'Linux':
        if not shutil.which('nm') or not shutil.which('readelf'):
            return None
        exported = run(['nm', '-D', '--defined-only', binary])
        imported = run(['nm', '-D', '--undefined-only', binary])
        libraries = re.findall(r'\(NEEDED\).*?\[(.*?)\]', run(['readelf', '-d', binary]))
    elif system == 'Darwin':
        if not shutil.which('nm') or not shutil.which('otool'):
            return None
        exported = run(['nm', '-gU', binary])
        imported = run(['nm', '-u', binary])
        libraries = [line.split()[0] for line in run(['otool', '-L', binary]).splitlines()[1:] if line.strip()]
    elif system == 'Windows':
        if not shutil.which('dumpbin'):
            return None
        exported = run(['dumpbin', '/nologo', '/exports', binary])
        imported = run(['dumpbin', '/nologo', '/imports', binary])
        libraries = re.findall(r'^\s+([\w.\-]+\.dll)\s*$', imported, re.MULTILINE | re.IGNORECASE)
    else:
        return None
    return (
        sorted(set(FFTW.findall(exported))),
        sorted(set(FFTW.findall(imported))),
        [name for name in libraries if 'fftw' in name.lower()],
    )


def main():
    backend, binary = sys.argv[1].upper(), sys.argv[2]
    found = symbols(binary)
    if found is None:
        print(f'No symbol tool for {platform.system()}; skipped.')
        return 77
    exported, imported, libraries = found
    problems = []
    if exported:
        problems.append(f'exports FFTW symbols other code could bind to: {exported[:5]}')
    if backend != 'FFTW' and (imported or libraries):
        problems.append(f'uses FFTW although the backend is {backend}: {(imported + libraries)[:5]}')
    if problems:
        print('\n'.join(problems))
        return 1
    note = ' (FFTW chosen explicitly; imports allowed)' if backend == 'FFTW' and (imported or libraries) else ''
    print(f'{binary}: no exported FFTW symbols; backend {backend}{note}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
