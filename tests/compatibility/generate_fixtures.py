#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write the compatibility corpus fixtures described by cases.json.

Fixtures are generated, never copied: impulse responses come from the integer or
float samples in the manifest, configuration text is repository-owned, and input
signals are deterministic. Only the Python standard library is used.

Usage: generate_fixtures.py OUTPUT_DIRECTORY [CASE_ID ...]
"""

import json
import struct
import sys
from pathlib import Path

MANIFEST = Path(__file__).resolve().with_name('cases.json')


def load_manifest(path=MANIFEST):
    with Path(path).open(encoding='utf-8') as f:
        manifest = json.load(f)
    if manifest.get('schema') != 1:
        raise ValueError('unsupported corpus manifest schema')
    return manifest


def write_wav(path, rate, channels, fmt):
    """Write interleaved WAV data from per-channel sample lists.

    fmt 'pcm16' takes integers (full scale 32768); 'float' takes floats.
    """
    frames = max((len(c) for c in channels), default=0)
    data = [c[f] if f < len(c) else 0 for f in range(frames) for c in channels]
    if fmt == 'pcm16':
        tag, width, payload = 1, 2, struct.pack(f'<{len(data)}h', *data)
    elif fmt == 'float':
        tag, width, payload = 3, 4, struct.pack(f'<{len(data)}f', *data)
    else:
        raise ValueError('unknown fixture format: ' + fmt)
    count = len(channels)
    header = struct.pack(
        '<4sI4s4sIHHIIHH4sI',
        b'RIFF',
        36 + len(payload),
        b'WAVE',
        b'fmt ',
        16,
        tag,
        count,
        rate,
        rate * count * width,
        count * width,
        width * 8,
        b'data',
        len(payload),
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + payload)


def signal(spec, channels):
    """Deterministic per-channel test input; values are exact in float32."""
    if 'samples' in spec:
        return [list(c) for c in spec['samples']]
    frames, kind = spec['frames'], spec.get('signal', 'noise')
    result = []
    for c in range(channels):
        if kind == 'noise':
            state = 0x9E3779B9 ^ (c * 0x85EBCA6B) ^ spec.get('seed', 0)
            values = []
            for _ in range(frames):
                state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
                values.append(((state >> 8) % 1025 - 512) / 1024)
            result.append(values)
        elif kind == 'last-frame-impulse':
            result.append([0.0] * (frames - 1) + [0.5 + 0.125 * c] if frames else [])
        else:
            raise ValueError('unknown signal: ' + kind)
    return result


def substitute(text, root):
    return text.replace('{root}', root.resolve().as_posix())


def generate_case(case, root):
    """Create one case's tree under root and return its input channel data."""
    root.mkdir(parents=True, exist_ok=True)
    for name, spec in case['files'].items():
        path = root / Path(name)
        path.parent.mkdir(parents=True, exist_ok=True)
        if 'text' in spec:
            text = substitute(spec['text'], root)
            if spec.get('crlf'):
                text = text.replace('\n', '\r\n')
            data = text.encode('utf-8')
            if spec.get('bom'):
                data = b'\xef\xbb\xbf' + data
            path.write_bytes(data)
        elif 'directory' in spec:
            path.mkdir(parents=True, exist_ok=True)
        else:
            write_wav(path, spec['rate'], spec['channels'], spec['format'])
    spec = case['input']
    channels = signal(spec, spec['channels'])
    write_wav(root / 'input.wav', spec['rate'], channels, 'float')
    (root / 'elsewhere').mkdir(exist_ok=True)
    return channels


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    manifest = load_manifest()
    wanted = set(argv[2:])
    out = Path(argv[1])
    for case in manifest['cases']:
        if not wanted or case['id'] in wanted:
            generate_case(case, out / case['id'])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
