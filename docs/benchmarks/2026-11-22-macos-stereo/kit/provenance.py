# SPDX-License-Identifier: GPL-2.0-or-later
"""List all matching stereo WAVs in the release tag that already advertised 40x."""

import hashlib
import struct
import subprocess
import sys
from pathlib import Path

repo = Path(sys.argv[1])
tag = '8c0814eb826fcc421fc61c9090891043f80169e5'
ls = subprocess.check_output(['git', '-C', str(repo), 'ls-tree', '-r', '-z', '--name-only', tag]).decode().split('\0')
count = 0
for path in ls:
    if not path.lower().endswith('.wav'):
        continue
    count += 1
    b = subprocess.check_output(['git', '-C', str(repo), 'show', tag + ':' + path])
    if b[:4] != b'RIFF' or b[8:12] != b'WAVE':
        raise ValueError(f'Unexpected WAV format in {path}')
    pos = 12
    fmt = None
    n = None
    while pos + 8 <= len(b):
        size = struct.unpack_from('<I', b, pos + 4)[0]
        if b[pos : pos + 4] == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', b, pos + 8)
        if b[pos : pos + 4] == b'data':
            n = size
        pos += 8 + size + (size % 2)
    if fmt is not None and n is not None and fmt[1] == 2 and fmt[2] == 44100 and n == 65536 * fmt[4]:
        print('Matching WAV:', path, 'SHA-256', hashlib.sha256(b).hexdigest())
print('WAVs checked in convolver2-19:', count)
