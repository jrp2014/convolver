#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare actual CLI output from two backend builds, without third-party Python."""
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import wave


def compare(first, second):
    rng = random.Random(0xC0FFEE)
    maximum = squares = 0.0
    samples = cases = 0
    with tempfile.TemporaryDirectory(prefix='convolver-backends-') as temp:
        root = Path(temp)
        for block in (1, 3, 17, 64, 128, 192, 256, 1024):
            length = 2*block+3 if block < 64 else 4097
            ir = [rng.randrange(-2000, 2001) for _ in range(length*2)]
            with wave.open(str(root/'ir.wav'), 'wb') as f:
                f.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
                f.writeframes(struct.pack('<%dh' % len(ir), *ir))
            config = root/'matrix.cfg'
            config.write_text('48000 3 4 0\n0 .1 .3\n.2 0 .4 .1\n'
                              'ir.wav\n0\n-0 1.5\n0 2.5\n'
                              'ir.wav\n1\n1 2.25\n-1 3\n')
            frames = 3*block+7
            raw = struct.pack('<%df' % (frames*3), *[rng.uniform(-.5, .5) for _ in range(frames*3)])
            for keep in (False, True):
                args = ['--filter', str(config), '--input', '-', '--raw-input', '48000', '3',
                        '--output', '-', '--output-format', 'raw-float', '--block-size', str(block)]
                if keep:
                    args.append('--keep-latency')
                outputs = [subprocess.run([str(exe), *args], input=raw, capture_output=True, check=True).stdout
                           for exe in (first, second)]
                assert len(outputs[0]) == len(outputs[1]) and len(outputs[0]) % 16 == 0, 'frame count mismatch'
                values = [struct.unpack('<%df' % (len(data)//4), data) for data in outputs]
                for a, b in zip(*values):
                    error = abs(a-b)
                    assert math.isfinite(a) and math.isfinite(b) and error <= 2e-5*(1+max(abs(a), abs(b))), (block, a, b)
                    maximum = max(maximum, error)
                    squares += error*error
                    samples += 1
                cases += 1
    return {'cases': cases, 'samples': samples, 'max_absolute_error': maximum,
            'rms_error': math.sqrt(squares/samples), 'tolerance': '2e-5*(1+max(abs(a),abs(b)))'}


if __name__ == '__main__':
    result = compare(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
    print(json.dumps(result, indent=2))
