#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ROBUST-10: torture-test impulse responses through the CLI.

Synthetic filters that real-world IRs rarely reach, generated here rather
than kept as large files: a 60 s noise tail, a unit impulse padded to many
partitions, a pure delay, a full-scale DC filter under full-scale DC input,
and a tail that decays through the subnormal range. Each failure names the
user-visible contract it breaks. Standard library only.

    torture_tests.py CONVOLVER
"""

import math
import platform
import random
import struct
import subprocess
import sys
import tempfile
from array import array
from pathlib import Path

RATE = 48000
FLT_MIN = 2.0**-126  # The smallest normal float.


def float_wave(path, samples, channels=1, rate=RATE):
    """A canonical 32-bit float WAV of interleaved `samples`."""
    data = array('f', samples)
    if sys.byteorder != 'little':
        data.byteswap()
    payload = data.tobytes()
    fmt = struct.pack('<HHIIHH', 3, channels, rate, rate * 4 * channels, 4 * channels, 32)
    body = b'WAVEfmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(payload)) + payload
    Path(path).write_bytes(b'RIFF' + struct.pack('<I', len(body)) + body)
    return path


def pcm_wave(path, values, bits, channels=1, rate=RATE):
    """An integer PCM WAV of interleaved `values`, written byte by byte."""
    width = bits // 8
    payload = b''.join(v.to_bytes(width, 'little', signed=True) for v in values)
    fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * width * channels, width * channels, bits)
    body = b'WAVEfmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(payload)) + payload
    Path(path).write_bytes(b'RIFF' + struct.pack('<I', len(body)) + body + (b'\0' if len(payload) & 1 else b''))
    return path


def read_wave(path):
    """(format tag, bits, channels, rate, samples): float samples as an array
    of doubles, integer ones as a tuple of ints. Other chunks are skipped."""
    data = Path(path).read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', path
    pos, fmt, payload = 12, None, None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            code, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', data, pos + 8)
            if code == 0xFFFE:
                code = struct.unpack_from('<H', data, pos + 32)[0]
            fmt = (code, bits, channels, rate)
        elif tag == b'data':
            payload = data[pos + 8 : pos + 8 + size]
        pos += 8 + size + (size & 1)
    assert fmt is not None and payload is not None, path
    code, bits, _, _ = fmt
    if code == 3:
        values = array('f')
        values.frombytes(payload)
        if sys.byteorder != 'little':
            values.byteswap()
        return (*fmt, array('d', values))
    width = bits // 8
    return (
        *fmt,
        tuple(int.from_bytes(payload[i : i + width], 'little', signed=True) for i in range(0, len(payload), width)),
    )


def render(exe, ir, source, out, *extra):
    """Standard error of a render that must succeed."""
    args = ['--filter', ir, '--input', source, '--output', out, *extra]
    p = subprocess.run([exe, *map(str, args)], capture_output=True, check=False)
    text = p.stderr.decode(errors='replace').replace('\r\n', '\n')
    assert p.returncode == 0, (args, p.returncode, text)
    return text


def to_float(x):
    """x rounded to single precision, as a float WAV stores it."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


# The filters. Each is deterministic, so a failure reproduces.


def long_noise_tail(seconds=60):
    """Seeded white noise decaying by 60 dB over `seconds`: a hall far longer
    than any real one, dense in every partition."""
    n = seconds * RATE
    r = random.Random(0x0E1)
    k = math.log(1e-3) / n
    return array('f', (r.uniform(-1, 1) * math.exp(k * i) for i in range(n)))


def subnormal_tail(seconds=1):
    """An alternating tail decaying from 1 to 1e-60, below the smallest normal
    float (1.2e-38) for about its last third; stored as float, its last
    quarter is zero."""
    n = seconds * RATE
    k = math.log(1e-60) / n
    return array('f', ((1 if i % 2 else -1) * math.exp(k * i) for i in range(n)))


def dirac(length, at=0):
    h = array('f', bytes(4 * length))
    h[at] = 1.0
    return h


def dc(length):
    return array('f', [1.0]) * length


# The checks.


def impulses(frames, hits):
    """Silence of `frames` frames with an impulse of each (frame, level)."""
    x = array('d', bytes(8 * frames))
    for at, level in hits:
        x[at] = level
    return x


def sparse_convolution(x, h):
    """x * h for sparse x, in double precision, in the order a direct sum adds."""
    y = array('d', bytes(8 * (len(x) + len(h) - 1)))
    for i, v in enumerate(x):
        if v:
            for j, w in enumerate(h):
                y[i + j] += v * w
    return y


def within(actual, expected, tolerance, contract):
    """Every frame within `tolerance` of the expected output's peak; returns
    the worst error relative to that peak, as native.engine reports it."""
    assert len(actual) == len(expected), (contract, 'frames', len(actual), len(expected))
    peak = max(abs(v) for v in expected)
    worst, where = 0.0, 0
    for i, (a, b) in enumerate(zip(actual, expected)):
        assert math.isfinite(a), (contract, 'non-finite sample', i, a)
        if abs(a - b) > worst:
            worst, where = abs(a - b), i
    assert worst <= tolerance * peak, (contract, 'frame', where, actual[where], expected[where], worst / peak)
    return worst / peak


def long_tail_tests(exe, d):
    """A 60 s dense tail renders every frame of its response, the last
    included, within float rounding: impulses in, shifted copies of the
    filter out (an exact oracle, cheap at this length)."""
    h = long_noise_tail()
    ir = float_wave(d / 'long.wav', h)
    x = impulses(4800, [(0, 0.5), (1000, -0.25), (4799, 0.125)])
    source = float_wave(d / 'long-in.wav', x)
    out = d / 'long-out.wav'
    render(exe, ir, source, out)
    *_, y = read_wave(out)
    expected = sparse_convolution(x, h)
    worst = within(y, expected, 1e-5, 'a 60 s tail renders in full, within float rounding')
    # The last frame is the last tap, a little above -60 dB: the tail is not cut short.
    assert abs(y[-1] - expected[-1]) <= 1e-5 * abs(expected[-1]) + 1e-7, ('the last tap reaches the output', y[-1])
    print(f'long tail: {len(h)} taps, worst error {worst:.3g} of the output peak')


def unit_impulse_tests(exe, d):
    """A unit impulse at the first tap passes float audio through unchanged,
    sample for sample, however much silence pads it and whatever the block
    size; the padding adds exact silence. A delayed impulse is a pure delay
    within float rounding, the last tap of a 10 s filter included."""
    r = random.Random(0xD1AC)
    x = array('d', (to_float(r.uniform(-1, 1)) for _ in range(20000)))
    x[0], x[1], x[2] = 1.0, -1.0, FLT_MIN  # Full scale both ways, and the smallest normal float.
    source = float_wave(d / 'dirac-in.wav', x)
    out = d / 'dirac-out.wav'
    for length, blocks in ((1, ['default']), (65536, ['default', '0', '37']), (2**20 + 1, ['default'])):
        ir = float_wave(d / f'dirac-{length}.wav', dirac(length))
        for block in blocks:
            render(exe, ir, source, out, *([] if block == 'default' else ['--block-size', block]))
            *_, y = read_wave(out)
            assert len(y) == len(x) + length - 1, ('the output runs for the filter length', length, len(y))
            first = next((i for i in range(len(x)) if y[i] != x[i]), None)
            assert first is None, (
                'a unit impulse passes audio unchanged',
                {'taps': length, 'block': block, 'frame': first, 'in': x[first], 'out': y[first]},
            )
            assert not any(y[len(x) :]), ('zero padding adds exact silence', length, block)
    for at in (40000, 10 * RATE - 1):
        ir = float_wave(d / f'delay-{at}.wav', dirac(at + 1, at))
        render(exe, ir, source, out)
        *_, y = read_wave(out)
        expected = array('d', bytes(8 * at)) + x
        worst = within(y, expected, 1e-5, f'an impulse at tap {at} delays the audio by {at} frames, unchanged')
        print(f'pure delay of {at} frames: worst error {worst:.3g} of the output peak')
    # Integer audio: 24-bit (no dither by default) and 16-bit without dither
    # come back bit for bit, the most negative and most positive values included.
    ir = float_wave(d / 'dirac-44k.wav', dirac(1), rate=44100)
    for bits, extra in ((24, []), (16, ['--dither', 'none'])):
        low, high = -(2 ** (bits - 1)), 2 ** (bits - 1) - 1
        values = [low, high, 0, 1, -1, low] + [r.randint(low, high) for _ in range(8000)]
        source = pcm_wave(d / f'dirac-in-{bits}.wav', values, bits, channels=2, rate=44100)
        out = d / f'dirac-out-{bits}.wav'
        render(exe, ir, source, out, '--output-format', f'wav-pcm{bits}', *extra)
        code, width, channels, rate, y = read_wave(out)
        assert (code, width, channels, rate) == (1, bits, 2, 44100), ('the output keeps the input format', bits)
        first = next((i for i, (a, b) in enumerate(zip(y, values)) if a != b), None)
        assert len(y) == len(values) and first is None, (
            f'a unit impulse leaves {bits}-bit audio bit-identical',
            first,
            None if first is None else (values[first], y[first]),
        )


def full_scale_dc_tests(exe, d):
    """A filter of 9,600 taps of 1.0 under full-scale DC of both signs: the
    largest output any filter of that length can give. Float output keeps
    every sample, 9,600 times full scale, and says so; no-clip brings it back
    within full scale, blaming any float rounding on rounding, not on the
    input; integer output with no-clip clips nothing."""
    n = 9600
    ir = float_wave(d / 'dc.wav', dc(n))
    x = array('d', [1.0]) * 20000 + array('d', [-1.0]) * 20000
    source = float_wave(d / 'dc-in.wav', x)
    # The exact response: a running sum of the input over n frames.
    sums = [0.0]
    for v in x:
        sums.append(sums[-1] + v)
    expected = array('d', (sums[min(t, len(x) - 1) + 1] - sums[max(0, t - n + 1)] for t in range(len(x) + n - 1)))
    out = d / 'dc-out.wav'
    err = render(exe, ir, source, out)
    *_, y = read_wave(out)
    worst = within(y, expected, 1e-5, 'float output keeps samples far beyond full scale, unclipped')
    assert 'samples above full scale' in err and 'peak +79.65 dBFS' in err, ('float output reports the overshoot', err)
    print(f'full-scale DC: peak {max(y):.1f}, worst error {worst:.3g} of the output peak')
    # no-clip: -20 log10(9600) exactly; full-scale input stays within full
    # scale up to the convolution's float rounding (2^-16 at most, GAIN-3).
    err = render(exe, ir, source, out, '--gain-db', 'no-clip')
    db = float(err.split('no-clip attenuation ')[1].split(' dB')[0])
    assert abs(db + 20 * math.log10(n)) < 1e-9, ('no-clip attenuates by the filter bound', err)
    *_, y = read_wave(out)
    peak = max(abs(v) for v in y)
    assert 1 - 1e-5 <= peak <= 1 + 2**-16, ('no-clip keeps full-scale input within full scale', peak)
    assert 'this input goes beyond full scale' not in err and 'The input goes beyond' not in err, (
        'full-scale input is not reported as beyond full scale',
        err,
    )
    if peak > 1:
        assert 'float rounding' in err and 'need not mean that the input went beyond full scale' in err, (
            'the overshoot is named as rounding',
            err,
        )
    for fmt in ('wav-pcm24', 'wav-pcm16'):
        err = render(exe, ir, source, out, '--gain-db', 'no-clip', '--output-format', fmt)
        assert 'clipped' not in err, (f'no-clip keeps full-scale input from clipping in {fmt}', err)


def subnormal_tail_tests(exe, d):
    """A tail that decays below the smallest normal float renders as the
    normal part of the filter: no output sample is subnormal, and the
    response matches the filter with its subnormal taps read as zero.
    (native.denormals checks that the flushing is in force; this checks it
    from a filter file to an output file.)"""
    h = subnormal_tail()
    ir = float_wave(d / 'subnormal.wav', h)
    x = impulses(100, [(0, 1.0), (37, -0.5)])
    source = float_wave(d / 'subnormal-in.wav', x)
    out = d / 'subnormal-out.wav'
    render(exe, ir, source, out)
    *_, y = read_wave(out)
    # Processing flushes subnormal numbers only where denormals.hpp has a
    # control for it: x86-64, and AArch64 except with MSVC. Elsewhere it keeps
    # IEEE gradual underflow, as native.denormals expects.
    machine = platform.machine().lower()
    flushes = machine in ('x86_64', 'amd64') or (machine in ('arm64', 'aarch64') and sys.platform != 'win32')
    subnormal = [i for i, v in enumerate(y) if v and abs(v) < FLT_MIN]
    assert not flushes or not subnormal, (
        'no output sample is subnormal',
        subnormal[:5],
        [y[i] for i in subnormal[:5]],
    )
    flushed = array('f', (w if abs(w) >= FLT_MIN else 0.0 for w in h))
    worst = within(
        y, sparse_convolution(x, flushed), 1e-5, 'a tail fading through the subnormal range renders as its normal part'
    )
    print(f'subnormal tail: worst error {worst:.3g} of the output peak')


def main():
    exe = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='convolver-torture-') as t:
        d = Path(t)
        for case in (
            unit_impulse_tests,
            full_scale_dc_tests,
            subnormal_tail_tests,
            long_tail_tests,
        ):
            case(exe, d)
    print('PASS: torture-test filters: 60 s tail, unit impulse, delays, full-scale DC, subnormal tail')


if __name__ == '__main__':
    main()
