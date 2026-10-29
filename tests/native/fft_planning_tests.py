#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real-library planning, fresh-process wisdom reuse and file protection."""

import math
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

exe = str(Path(sys.argv[1]).resolve())


def run(*args, ok=True):
    p = subprocess.run([exe, *map(str, args)], capture_output=True, timeout=60, check=False)
    assert (p.returncode == 0) == ok, (args, p.returncode, p.stderr.decode())
    return p


def wav(path, samples):
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(48000)
        f.writeframes(struct.pack('<' + 'h' * len(samples), *samples))


with tempfile.TemporaryDirectory(prefix='convolver-planning-') as directory:
    root = Path(directory)
    impulse, audio = root / 'impulse.wav', root / 'input.wav'
    h = [int(3000 * math.cos(i * 0.21)) for i in range(137)]
    x = [int(5000 * math.sin(i * 0.31)) for i in range(219)]
    wav(impulse, h)
    wav(audio, x)
    base = ['--filter', impulse, '--input', audio, '--output-format', 'raw-float']
    wisdom = root / 'plans-µ.txt'
    fftw = 'FFT backend: FFTW' in run('--version').stdout.decode()
    if fftw:
        for plan in ('measure', 'patient', 'exhaustive'):
            for block in (17, 64):
                saved = root / f'{plan}-{block}.txt'
                run('--tune-fft', saved, '--block-size', block, '--fft-plan', plan)
                assert 'fftw' in saved.read_text().lower()
                # A separate process must reconstruct both plans solely from disk.
                for mode in ([], ['--fft-wisdom', saved]):
                    out = root / 'out.raw'
                    run(*base, '--output', out, '--block-size', block, '--fft-plan', plan, *mode)
                    actual = struct.unpack('<' + 'f' * (out.stat().st_size // 4), out.read_bytes())
                    expected = [0.0] * (len(x) + len(h) - 1)
                    for i, v in enumerate(x):
                        for j, w in enumerate(h):
                            expected[i + j] += (v / 32768) * (w / 32768)
                    assert len(actual) == len(expected)
                    assert all(math.isfinite(a) and abs(a - b) <= 2e-5 * (1 + abs(b)) for a, b in zip(actual, expected))
                # A missing size must fail before opening output, even if output exists.
                out.write_bytes(b'preserve me')
                p = run(*base, '--output', out, '--block-size', 131, '--fft-wisdom', saved, ok=False)
                assert 'wisdom' in p.stderr.decode().lower()
                assert out.read_bytes() == b'preserve me'
                if plan == 'measure':
                    run(
                        *base,
                        '--output',
                        out,
                        '--block-size',
                        block,
                        '--fft-wisdom',
                        saved,
                        '--fft-plan',
                        'patient',
                        ok=False,
                    )
                    assert out.read_bytes() == b'preserve me'
        # Time-limited planning: the historical "Take 1 minute" mode, shortened.
        limited = root / 'limited.txt'
        run('--tune-fft', limited, '--block-size', 32, '--fft-plan', 'exhaustive', '--fft-time-limit', '0.5')
        run(
            *base,
            '--output',
            root / 'limited.raw',
            '--block-size',
            32,
            '--fft-plan',
            'patient',
            '--fft-time-limit',
            '0.25',
        )
        for bad in ('0', '-1', 'nan', 'x', '86401'):
            run(*base, '--output', root / 'bad-limit.raw', '--fft-plan', 'measure', '--fft-time-limit', bad, ok=False)
        run(*base, '--output', root / 'bad-limit.raw', '--fft-time-limit', '1', ok=False)
        assert not (root / 'bad-limit.raw').exists()
        # Default tuning is patient; rendering defaults to estimate, which accepts it.
        run('--tune-fft', wisdom, '--block-size', 16)
        run(*base, '--output', root / 'default.raw', '--block-size', 16, '--fft-wisdom', wisdom)
        before = wisdom.read_bytes()
        run('--tune-fft', wisdom, '--block-size', 16, ok=False)
        run(*base, '--output', wisdom, '--block-size', 16, '--fft-wisdom', wisdom, ok=False)
        assert wisdom.read_bytes() == before
        for payload in (b'garbage', b'(fftw-3.3.11 fftwf_wisdom)\0junk', b'x' * (1024 * 1024 + 1)):
            bad = root / 'bad.txt'
            bad.write_bytes(payload)
            dest = root / 'not-created.raw'
            run(*base, '--output', dest, '--fft-wisdom', bad, ok=False)
            assert not dest.exists()
        run(*base, '--output', root / 'missing.raw', '--fft-wisdom', root / 'missing.txt', ok=False)
    else:
        for args in (
            ['--tune-fft', wisdom],
            [*base, '--output', wisdom, '--fft-plan', 'measure'],
            [*base, '--output', wisdom, '--fft-wisdom', impulse],
        ):
            p = run(*args, ok=False)
            assert 'FFTW' in p.stderr.decode()
            assert not wisdom.exists()
    for target in (impulse, audio):
        before = target.read_bytes()
        run('--tune-fft', target, ok=False)
        assert target.read_bytes() == before
    for args in (
        ['--tune-fft', root / 'invalid.txt', '--fft-plan', 'invalid'],
        ['--tune-fft', root / 'invalid.txt', '--block-size', 1048577],
        ['--tune-fft', '-'],
        ['--tune-fft', root / 'invalid.txt', '--fft-plan', 'estimate'],
        ['--tune-fft', root / 'invalid.txt', '--input', audio],
        ['--tune-fft', root / 'invalid.txt', '--fft-wisdom', impulse],
    ):
        run(*args, ok=False)
        assert not (root / 'invalid.txt').exists()

print('FFT planning, wisdom persistence, direct convolution and error handling passed')
