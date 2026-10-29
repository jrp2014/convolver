#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual restored pipe main and helpers with portable I/O adapters.

Exercises argument validation, stream/short-write failures, bounded chunks and
EOF tail accounting. Windows/libsndfile and FFT execution are adapter boundaries;
core_regressions.py separately exercises the production convolution kernels.
"""

import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    source = (ROOT / 'historical/convolverPipe/convolverPipe.cpp').read_text()
    # Remove platform includes only, preserving the complete real entry point.
    source = re.sub(r'^#include.*$', '', source, flags=re.M)
    helpers = (ROOT / 'historical/convolverPipe/pipe_helpers.h').read_text()
    helpers = helpers.replace('#pragma once', '')
    harness = (
        (HERE / 'pipe_regressions.cpp').read_text().replace('// INSERT PRODUCTION ENTRY POINT', helpers + '\n' + source)
    )
    with tempfile.TemporaryDirectory(prefix='convolver-pipe-') as directory:
        cpp = Path(directory) / 'pipe.cpp'
        binary = Path(directory) / 'pipe'
        cpp.write_text(harness)
        subprocess.run(
            [
                os.environ.get('CXX', 'clang++'),
                '-std=c++17',
                '-O1',
                '-g',
                '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer',
                str(cpp),
                '-o',
                str(binary),
            ],
            check=True,
        )
        return subprocess.run([str(binary)]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
