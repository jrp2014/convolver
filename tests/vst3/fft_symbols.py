# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that a plug-in binary cannot share an FFTW planner with its host.

Usage: fft_symbols.py BACKEND PLUGIN_BINARY

A plug-in must never export FFTW symbols: a host or another plug-in could bind
to them. Unless the build deliberately chose FFTW, it must not import FFTW
either. Exit code 77 means the platform's symbol tool was not found (skipped).
"""

import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

FFTW = re.compile(r'\b_?fftwf?_\w+')


def run(command):
    return subprocess.run(
        command, check=True, text=True, errors='replace', stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    ).stdout


def dumpbin():
    """dumpbin from PATH, or else from the newest Visual Studio C++ tools.

    CTest runs outside a developer prompt in CI, so dumpbin is rarely on PATH
    there (CI-23); vswhere, which every Visual Studio installs, finds it.
    """
    if found := shutil.which('dumpbin'):
        return found
    vswhere = Path(os.environ.get('PROGRAMFILES(X86)', r'C:\Program Files (x86)'))
    vswhere = vswhere / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if not vswhere.is_file():
        return None
    pattern = r'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe'
    command = [str(vswhere), '-latest', '-products', '*', '-find', pattern]
    try:
        output = subprocess.run(command, check=False, text=True, capture_output=True).stdout
    except OSError:  # vswhere could not start: no tool, so skipped.
        return None
    found = sorted(line.strip() for line in output.splitlines() if line.strip())
    return found[-1] if found else None


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
        tool = dumpbin()
        if not tool:
            return None
        exported = run([tool, '/nologo', '/exports', binary])
        imported = run([tool, '/nologo', '/imports', binary])
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
