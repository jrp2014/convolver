# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import struct
from pathlib import Path

for name in ['Filter', '32bitFilterDRC', 'PerfectDiracDelta-48000-1-65535']:
    p = Path('current/historical/Media') / (name + '.wav')
    data = p.read_bytes()
    pos = 12
    info = None
    samples = None
    while pos + 8 <= len(data):
        (size,) = struct.unpack_from('<I', data, pos + 4)
        chunk = data[pos + 8 : pos + 8 + size]
        if data[pos : pos + 4] == b'fmt ':
            info = struct.unpack_from('<HHIIHH', chunk)
        if data[pos : pos + 4] == b'data':
            samples = chunk
        pos += 8 + size + (size % 2)
    if not info or not samples or info[0] != 3 or info[5] != 32 or len(samples) != 65536 * info[4]:
        raise ValueError(f'Unexpected WAV format or frame count in {p}')
    print(name, info[2], info[1], 65536, hashlib.sha256(data).hexdigest())
    Path(name + '.f32').write_bytes(samples)
