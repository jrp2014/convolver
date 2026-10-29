# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that the LV2 library exports lv2_descriptor and nothing else.

Usage: exports.py PLUGIN_BINARY

Hosts load many plug-ins into one process, sometimes both Convolver's VST3 and
LV2 builds, or two versions of one. Exporting only the entry point keeps them
from binding to each other's internals. Exit code 77 means the platform's
symbol tool was not found (skipped).
"""

import platform
import shutil
import subprocess
import sys


def exported(binary):
    system = platform.system()
    if system == 'Linux' and shutil.which('nm'):
        output = subprocess.run(
            ['nm', '-D', '--defined-only', binary], check=True, text=True, stdout=subprocess.PIPE
        ).stdout
        names = [line.split()[-1] for line in output.splitlines() if line.strip()]
        # Version-script nodes and linker-defined markers are not functions or data.
        return sorted(n for n in names if not n.startswith(('_init', '_fini', '_edata', '_end', '__bss_start')))
    if system == 'Darwin' and shutil.which('nm'):
        output = subprocess.run(['nm', '-gU', binary], check=True, text=True, stdout=subprocess.PIPE).stdout
        return sorted(line.split()[-1].lstrip('_') for line in output.splitlines() if line.strip())
    return None


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    names = exported(argv[1])
    if names is None:
        print('no symbol tool for this platform; skipped')
        return 77
    if names != ['lv2_descriptor']:
        print(f'{argv[1]} exports {len(names)} symbols, expected only lv2_descriptor:')
        for name in names[:40]:
            print(f'  {name}')
        return 1
    print(f'{argv[1]} exports only lv2_descriptor')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
