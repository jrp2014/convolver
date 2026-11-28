#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""End-to-end CLI checks through real FFTW and libsndfile, no Python packages."""

import json
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

EXE = str(Path(sys.argv[1]).resolve())


def pcm(path, values, channels=1, rate=48000):
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(channels)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(struct.pack(f'<{len(values)}h', *values))


def floats(path):
    data = Path(path).read_bytes()
    assert data[:4] in (b'RIFF', b'RF64') and data[8:12] == b'WAVE'
    pos = 12
    payload = None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            fmt, channels, rate = struct.unpack_from('<HHI', data, pos + 8)
            assert fmt in (3, 65534), fmt
        if tag == b'data':
            payload = data[pos + 8 : pos + 8 + size]
        pos += 8 + size + (size & 1)
    assert payload is not None
    return channels, rate, list(struct.unpack(f'<{len(payload) // 4}f', payload))


def wav_audio(path):
    """The fmt fields and decoded samples of a WAV file, ignoring other chunks.

    Other chunks (libsndfile's PEAK chunk records the second the file was
    written) are incidental metadata, not part of the audio.
    """
    data = Path(path).read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', path  # RF64 sizes are in ds64; not read here.
    pos, fmt, payload = 12, None, None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            tag_code, channels, rate, _, align, bits = struct.unpack_from('<HHIIHH', data, pos + 8)
            valid = bits
            if tag_code == 65534:
                valid = struct.unpack_from('<H', data, pos + 26)[0]
                tag_code = struct.unpack_from('<H', data, pos + 32)[0]
            fmt = {'format': tag_code, 'channels': channels, 'rate': rate, 'bits': bits, 'valid bits': valid}
        if tag == b'data':
            payload = data[pos + 8 : pos + 8 + size]
        pos += 8 + size + (size & 1)
    assert fmt is not None and payload is not None, path
    assert (fmt['format'], fmt['bits']) in {(1, 8), (1, 16), (1, 24), (1, 32), (3, 32)}, (path, fmt)
    width = fmt['bits'] // 8
    fmt['frames'] = len(payload) // align
    if fmt['format'] == 3:
        samples = struct.unpack(f'<{len(payload) // 4}f', payload)
    elif width == 1:
        samples = tuple(b - 128 for b in payload)
    else:
        samples = tuple(
            int.from_bytes(payload[i : i + width], 'little', signed=True) for i in range(0, len(payload), width)
        )
    return fmt, samples, payload


def same_audio(a, b, label):
    """Two WAV files hold the same audio: format, rate, channels and frame count
    checked one by one, then every sample exactly (as stored, so -0.0 is not
    0.0); the decoded values only describe a difference."""
    fa, sa, ra = wav_audio(a)
    fb, sb, rb = wav_audio(b)
    for key in fa:
        assert fa[key] == fb[key], (label, f'{key} differs', fa[key], fb[key])
    if ra != rb:
        width = fa['bits'] // 8
        first = next(
            i for i in range(len(ra) // width) if ra[i * width : (i + 1) * width] != rb[i * width : (i + 1) * width]
        )
        worst = max(abs(x - y) for x, y in zip(sa, sb))
        where = f'frame {first // fa["channels"]} channel {first % fa["channels"]}'
        detail = f'first difference at {where}: {sa[first]} != {sb[first]}; largest difference {worst}'
        raise AssertionError((label, detail))


def near(actual, expected):
    assert len(actual) == len(expected), (len(actual), len(expected))
    for i, (a, b) in enumerate(zip(actual, expected)):
        assert math.isfinite(a) and abs(a - b) <= 3e-5 + 3e-5 * abs(b), (i, a, b)


def call(args, *, ok=True, data=None):
    p = subprocess.run([EXE, *map(str, args)], input=data, capture_output=True, check=False)
    assert (p.returncode == 0) == ok, (args, p.returncode, p.stderr.decode(errors='replace'))
    return p


def channel_mask(path):
    """dwChannelMask from an extensible fmt chunk, parsed without libsndfile; None if plain."""
    data = Path(path).read_bytes()
    pos = 12
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            fmt = struct.unpack_from('<H', data, pos + 8)[0]
            return struct.unpack_from('<I', data, pos + 28)[0] if fmt == 0xFFFE else None
        pos += 8 + size + (size & 1)
    raise AssertionError('no fmt chunk')


def wave_fields(path):
    """(format tag, container bits, valid bits, mask, raw sample bytes) without libsndfile."""
    data = Path(path).read_bytes()
    pos = 12
    fields = None
    payload = None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            fmt, _, _, _, _, bits = struct.unpack_from('<HHIIHH', data, pos + 8)
            valid, mask = struct.unpack_from('<HI', data, pos + 26) if fmt == 0xFFFE else (bits, 0)
            fields = (fmt, bits, valid, mask)
        if tag == b'data':
            payload = data[pos + 8 : pos + 8 + size]
        pos += 8 + size + (size & 1)
    return (*fields, payload)


def float_wave(path, frames, channels=1, rate=48000):
    """A 32-bit float WAV of `frames` (tuples of `channels` samples), NaN and infinity kept."""
    data = b''.join(struct.pack(f'<{channels}f', *frame) for frame in frames)
    fmt = struct.pack('<HHIIHH', 3, channels, rate, rate * 4 * channels, 4 * channels, 32)
    body = b'WAVEfmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(data)) + data
    Path(path).write_bytes(b'RIFF' + struct.pack('<I', len(body)) + body)


def failure_message_tests(d):
    """DIAG-1: each failure names the file or value and says what to change."""
    ir = d / 'msg-ir.wav'
    pcm(ir, [16384])
    stereo_ir = d / 'msg-ir2.wav'
    pcm(stereo_ir, [16384, 16384], channels=2)
    good = d / 'msg-in.wav'
    pcm(good, [1000, -1000, 500])
    out = d / 'msg-out.wav'
    nan_input = d / 'msg-nan.wav'
    float_wave(nan_input, [(0.5, 0.5), (0.25, float('nan')), (0.0, 0.0)], channels=2)
    loud = d / 'msg-loud.wav'
    float_wave(loud, [(3e38,), (3e38,)])
    garbage = d / 'msg-garbage.wav'
    garbage.write_bytes(b'RIFF\0\1\2\3' + b'\xff' * 64)
    truncated = d / 'msg-truncated.wav'
    pcm(truncated, list(range(100)))
    truncated.write_bytes(truncated.read_bytes()[:-20])
    damaged_cfg = d / 'msg-damaged.cfg'
    damaged_cfg.write_text('48000 1 1 0\n0\n0\nmsg-truncated.wav\n0\n0\n0\n')
    binary = d / 'msg-binary.bin'
    binary.write_bytes(bytes(range(256)))
    # A whole WAV whose format chunk names an unknown encoding: libsndfile's own refusal.
    bad_fmt = d / 'msg-fmt.wav'
    float_wave(bad_fmt, [(0.5,), (0.25,)])
    data = bytearray(bad_fmt.read_bytes())
    data[20] = 0x99
    bad_fmt.write_bytes(bytes(data))
    nan_ir = d / 'msg-nan-ir.wav'
    float_wave(nan_ir, [(0.5,), (float('inf'),)])
    # 16-bit samples read as the default float32 include a NaN pattern (0x7fc0).
    s16 = d / 'msg-s16.raw'
    s16.write_bytes(struct.pack('<4h', 0, 0x7FC0, 0, 0x7FC0))
    raw_args = ['--input', '-', '--raw-input', '48000', '1', '--output', '-', '--output-format', 'raw-float']
    cases = (
        # (arguments, standard input, texts the message must hold, output incomplete)
        (
            ['--filter', stereo_ir, '--input', nan_input, '--output', out],
            None,
            [str(nan_input), 'at 0.000 s (frame 1), channel 2', 'Export it again'],
            True,
        ),
        (
            ['--filter', ir, '--input', loud, '--output', out, '--gain-db', '12'],
            None,
            ['output overflowed at 0.000 s (frame 0), channel 1', 'Lower --gain-db', 'no-clip'],
            True,
        ),
        (
            ['--filter', ir, *raw_args],
            struct.pack('<2f', 0.5, float('inf')),
            ['from standard input', 'frame 1', 'read as f32le'],
            True,
        ),
        (['--filter', ir, '--input', garbage, '--output', out], None, [str(garbage), 'Export it again'], False),
        (['--filter', ir, '--input', bad_fmt, '--output', out], None, [str(bad_fmt), 'Export it again'], False),
        (
            ['--filter', nan_ir, '--input', good, '--output', out],
            None,
            ['cannot read sound file', 'frame 1), channel 1', 'Export it again'],
            False,
        ),
        (
            ['--filter', ir, '--input', s16, '--raw-input', '48000', '1', '--output', out],
            None,
            ['read as f32le', '--raw-format'],
            True,
        ),
        (
            ['--filter', ir, '--input', truncated, '--output', out],
            None,
            [str(truncated), 'RIFF/WAVE', 'Export it again'],
            False,
        ),
        (
            ['--filter', damaged_cfg, '--input', good, '--output', out],
            None,
            ['msg-truncated.wav', 'Export it again'],
            False,
        ),
        (
            ['--filter', binary, '--input', good, '--output', out],
            None,
            ['neither a configuration', 'export it again from the program'],
            False,
        ),
        (['--filter', ir, *raw_args], b'\0\0\0', ['incomplete frame', 'channel count and sample type'], True),
    )
    for args, data, texts, incomplete in cases:
        p = call(args, ok=False, data=data)
        message = p.stderr.decode()
        for text in texts:
            assert text in message, (args, text, message)
        # DIAG-2: the problem on the first line, then indented labelled lines with a fix.
        assert message.startswith('convolver: error: '), message
        assert '\n  Fix: ' in message, message
        shown = 'standard output' if args[args.index('--output') + 1] == '-' else str(out)
        assert ('is incomplete' in message and shown in message) == incomplete, (args, message)
    # The CLI replaces an existing output, as command-line tools usually do.
    out.write_bytes(b'not audio')
    call(['--filter', ir, '--input', good, '--output', out])
    assert floats(out)[0] == 1


def other_writers_tests(d):
    """IO-10: a WAV as Python's wave module wrote it before Python 3.15 for
    mono 24-bit audio with an odd number of frames (an odd data chunk with no
    pad byte) is read whole, as the filter and as the input. The file is
    written byte by byte, since Python 3.15's wave module adds the pad byte
    (CI-43)."""
    taps = [4194304, -2097152, 0, 1048576, 524288]
    odd = d / 'odd-24.wav'
    data = b''.join(struct.pack('<i', v)[:3] for v in taps)
    # PCM, 1 channel, 48 kHz, 144000 bytes a second, 3-byte frames, 24 bits.
    fmt = struct.pack('<HHIIHH', 1, 1, 48000, 48000 * 3, 3, 24)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data
    odd.write_bytes(b'RIFF' + struct.pack('<I', len(body)) + body)
    size = odd.stat().st_size
    assert size % 2 == 1 and struct.unpack_from('<I', odd.read_bytes(), 4)[0] == size - 8, size
    other = d / 'odd-other.wav'
    values = [12000, -6000, 3000, 1500, -750, 0, 375]
    pcm(other, values)
    out = d / 'odd-out.wav'
    expected = [0.0] * (len(values) + len(taps) - 1)
    for i, v in enumerate(values):
        for j, w in enumerate(taps):
            expected[i + j] += v / 32768 * w / 8388608
    for filter_file, input_file in ((odd, other), (other, odd)):
        call(['--filter', filter_file, '--input', input_file, '--output', out])
        channels, rate, y = floats(out)
        assert (channels, rate) == (1, 48000)
        near(y, expected)
        out.unlink()


def refused_sound_filter_tests(d):
    """DIAG-6: a sound file refused as a filter gives its reader's reason."""
    good = d / 'refused-in.wav'
    pcm(good, [1000, -1000, 500])
    out = d / 'refused-out.wav'
    garbage = d / 'refused-garbage.wav'
    garbage.write_bytes(b'RIFF\0\1\2\3' + b'\xff' * 64)
    truncated = d / 'refused-truncated.wav'
    pcm(truncated, list(range(100)))
    truncated.write_bytes(truncated.read_bytes()[:-20])
    wide = d / 'refused-wide.wav'
    pcm(wide, [0] * 300, channels=300)
    cases = (
        (garbage, ['refused-garbage.wav', 'Format not recognised', 'Export it again']),
        (truncated, ['refused-truncated.wav', 'truncated or invalid RIFF/WAVE size']),
        (wide, ['refused-wide.wav', 'channels 1..256']),
    )
    for ir, texts in cases:
        message = call(['--filter', ir, '--input', good, '--output', out], ok=False).stderr.decode()
        assert 'neither a configuration' not in message, (ir, message)
        for text in texts:
            assert text in message, (ir, text, message)
        # The location names the file; the reader's copy of its path is left out.
        assert message.count(ir.name) == 1, (ir, message)
        for extra in ([], ['--syntax-only']):
            report = json.loads(call(['--inspect', ir, '--json', *extra], ok=False).stdout)
            assert report['diagnostics'][0]['code'] == 'unreadable-asset', (ir, extra, report)
    # A container marker alone does not make a file a sound file: a list
    # whose first entry starts with FORM is still read as a list.
    (d / 'FORMAT.cfg').write_text('48000 1 1 0\n0\n0\nrefused-in.wav\n0\n0\n0\n')
    listed = d / 'refused-list.txt'
    listed.write_text('FORMAT.cfg\n')
    call(['--filter', listed, '--input', good, '--output', out])


def default_layout_tests(d):
    """EDGE-3: a mono IR file filters each channel of stereo audio, and a
    four-channel one plays as true stereo, each saying so on stderr; an exact
    match in a list still wins, and --inspect reports the layout."""
    src = d / 'dl-in.wav'
    dst = d / 'dl-out.wav'
    pcm(src, [8192, -4096, 4096, 2048, -8192, 1024], channels=2)
    left, right = [8192 / 32768, 4096 / 32768, -8192 / 32768], [-4096 / 32768, 2048 / 32768, 1024 / 32768]

    def convolve(x, h):
        y = [0.0] * (len(x) + len(h) - 1)
        for i, v in enumerate(x):
            for j, w in enumerate(h):
                y[i + j] += v * w
        return y

    def interleave(a, b):
        return [v for pair in zip(a, b) for v in pair]

    mono = [0.5, -0.25]
    float_wave(d / 'dl-mono.wav', [(v,) for v in mono])
    p = call(['--filter', d / 'dl-mono.wav', '--input', src, '--output', dst])
    assert b'filters each of the 2 input channels on its own' in p.stderr, p.stderr
    channels, _, y = floats(dst)
    assert channels == 2
    near(y, interleave(convolve(left, mono), convolve(right, mono)))
    # True stereo: channels left to left, left to right, right to left, right to right.
    ll, lr, rl, rr = [0.5, 0.25], [0.125, 0.0], [-0.25, 0.0625], [1.0, -0.5]
    float_wave(d / 'dl-hall.wav', list(zip(ll, lr, rl, rr)), channels=4)
    p = call(['--filter', d / 'dl-hall.wav', '--input', src, '--output', dst])
    assert b'plays as true stereo: left to left, left to right, right to left, right to right' in p.stderr, p.stderr
    out_left = [a + b for a, b in zip(convolve(left, ll), convolve(right, rl))]
    out_right = [a + b for a, b in zip(convolve(left, lr), convolve(right, rr))]
    near(floats(dst)[2], interleave(out_left, out_right))
    # An exact match anywhere in a list wins, with no note.
    float_wave(d / 'dl-stereo.wav', [(1.0, 0.5)], channels=2)
    (d / 'dl-list.txt').write_text('dl-mono.wav\ndl-stereo.wav\n')
    p = call(['--filter', d / 'dl-list.txt', '--input', src, '--output', dst])
    assert b'on its own' not in p.stderr, p.stderr
    near(floats(dst)[2], interleave(left, [0.5 * v for v in right]))
    report = json.loads(call(['--inspect', d / 'dl-hall.wav', '--input-channels', '2', '--json']).stdout)
    entry = report['configurations'][0]
    assert entry['layout'] == 'true-stereo' and entry['input_channels'] == 2, entry
    assert [(path['inputs'][0]['channel'], path['outputs'][0]['channel']) for path in entry['paths']] == [
        (0, 0),
        (0, 1),
        (1, 0),
        (1, 1),
    ], entry['paths']
    report = json.loads(call(['--inspect', d / 'dl-list.txt', '--input-channels', '2', '--json']).stdout)
    assert report['configurations'][0]['layout'] == 'as-written', report
    (d / 'dl-mono-list.txt').write_text('dl-hall.wav\ndl-mono.wav\n')
    report = json.loads(call(['--inspect', d / 'dl-mono-list.txt', '--input-channels', '3', '--json']).stdout)
    assert report['configurations'][0]['layout'] == 'each-channel', report
    traversal = report['selection']['traversal']
    assert [step['outcome'] for step in traversal] == ['list', 'input-mismatch', 'selected'], traversal
    # Validated as it renders, laid out on the requested channels: 2,049
    # paths are within the engine's 4,096 for one channel, not for two.
    (d / 'dl-many.cfg').write_text('48000 1 1 0\n0\n0\n' + 'dl-mono.wav\n0\n0\n0\n' * 2049)
    call(['--validate', d / 'dl-many.cfg'])
    p = call(['--validate', d / 'dl-many.cfg', '--input-channels', '2'], ok=False)
    assert b'4096' in p.stdout + p.stderr or b'4,096' in p.stdout + p.stderr, p.stdout + p.stderr


def integer_format_tests(d):
    """Dither, noise shaping, PCM8 and valid bits through the CLI."""
    ir = d / 'if-ir.wav'
    src = d / 'if-in.wav'
    dst = d / 'if-out.wav'
    pcm(ir, [32767])
    pcm(src, [(((i * 379) % 12001) - 6000) for i in range(3000)])
    base = ['--filter', ir, '--input', src, '--output', dst]
    call([*base, '--output-format', 'wav-pcm8', '--dither', 'none'])
    fmt, bits, valid, mask, payload = wave_fields(dst)
    assert (fmt, bits, valid) == (1, 8, 8) and len(payload) == 3000
    exact = [(((i * 379) % 12001) - 6000) * 32767 / 32768 / 256 for i in range(3000)]
    assert all(abs((b - 128) - x) <= 0.5 + 1e-6 for b, x in zip(payload, exact))
    for fmt_name, container in (('wav-pcm24', 24), ('wav-pcm32', 32)):
        for shaping in ('simple', 'third-order', 'ninth-order'):
            call(
                [
                    *base,
                    '--output-format',
                    fmt_name,
                    '--valid-bits',
                    '16',
                    '--dither',
                    'triangular',
                    '--noise-shaping',
                    shaping,
                ]
            )
            fmt, bits, valid, mask, payload = wave_fields(dst)
            assert (fmt, bits, valid, mask) == (0xFFFE, container, 16, 0), (fmt, bits, valid, mask)
            width = container // 8
            values = [
                int.from_bytes(payload[i : i + width], 'little', signed=True) for i in range(0, len(payload), width)
            ]
            assert all(v % (1 << (container - 16)) == 0 for v in values)
            assert values != [0] * len(values)
    # From 20 bits dither is off by default, still available, and called pointless.
    p = call([*base, '--output-format', 'wav-pcm24', '--dither', 'triangular'])
    fmt, bits, valid, mask, _ = wave_fields(dst)
    assert (fmt, bits, valid) == (1, 24, 24)
    assert b'pointless at 24 significant bits' in p.stderr, p.stderr
    p = call([*base, '--output-format', 'wav-pcm24', '--valid-bits', '20', '--noise-shaping', 'simple'])
    assert b'pointless at 20 significant bits' in p.stderr, p.stderr
    p = call([*base, '--output-format', 'wav-pcm32', '--dither', 'triangular'])
    assert b'exact above -48 dBFS' in p.stderr, p.stderr
    assert not call([*base, '--output-format', 'wav-pcm24']).stderr
    # Below 20 bits TPDF dither is the default, and the note says so.
    p = call([*base, '--output-format', 'wav-pcm24', '--valid-bits', '19'])
    assert b'19-bit output: adding triangular (TPDF) dither' in p.stderr, p.stderr
    dithered_default = wave_fields(dst)[4]
    call([*base, '--output-format', 'wav-pcm24', '--valid-bits', '19', '--dither', 'triangular'])
    assert wave_fields(dst)[4] == dithered_default
    # At 20 valid bits the default is plain rounding, the same as --dither none.
    assert not call([*base, '--output-format', 'wav-pcm24', '--valid-bits', '20']).stderr
    plain_default = wave_fields(dst)[4]
    call([*base, '--output-format', 'wav-pcm24', '--valid-bits', '20', '--dither', 'none'])
    assert wave_fields(dst)[4] == plain_default and plain_default != dithered_default


def automatic_gain_tests(d):
    """--gain-db auto/no-clip apply the attenuation estimates and report them."""
    ir = d / 'ag-ir.wav'
    src = d / 'ag-in.wav'
    dst = d / 'ag-out.wav'
    pcm(ir, [8192])
    values = [(((i * 379) % 12001) - 6000) for i in range(3000)]
    pcm(src, values)
    for mode in ('no-clip', 'auto'):
        p = call(['--filter', ir, '--input', src, '--output', dst, '--gain-db', mode])
        err = p.stderr.decode()
        # A 0.25 impulse: the largest possible peak and the noise peak are both ~0.25.
        db = float(re.search(r'attenuation (-?[0-9.]+) dB', err).group(1))
        assert abs(db - 12.04) < 0.02, err
        _, _, out = floats(dst)
        near(out, [v / 32768 * 0.25 * 10 ** (db / 20) for v in values]) if mode == 'no-clip' else None
    call(['--filter', ir, '--input', src, '--output', dst, '--gain-db', 'loud'], ok=False)
    # GAIN-2: no-clip takes the bound without filtering noise. Its gain must be
    # -20 log10 of the largest output any full-scale input can give (per output,
    # the absolute taps of each input's combined response, summed over inputs;
    # delays do not change it), and 0 dB when routing is silent or cancels out.
    pcm(d / 'ag-a.wav', [16384, -8192])  # 0.5, -0.25
    pcm(d / 'ag-b.wav', [-16384, 8192])  # The same, inverted.
    pcm(d / 'ag-zero.wav', [0, 0])
    stereo = d / 'ag-stereo.wav'
    pcm(stereo, [v for v in values for _ in range(2)], channels=2)

    def path(ir_file, inputs, outputs):
        return f'{ir_file}\n0\n{inputs}\n{outputs}\n'

    cases = {
        'cancelling': (
            '48000 1 1 0\n0\n0\n' + path('ag-a.wav', '0', '0') + path('ag-b.wav', '0', '0'),
            src,
            0.0,
        ),
        'delayed': ('48000 1 1 0\n5\n2\n' + path('ag-a.wav', '0', '0'), src, 0.75),
        # Output 0: input 0 through a (0.75) plus input 1 through a at gain 0.5
        # (0.375). Output 1: input 1 through b and through a at gain -0.25,
        # which add to [-0.625, 0.3125] (0.9375), so output 0 is the larger.
        'multichannel': (
            '48000 2 2 0\n0 0\n0 0\n'
            + path('ag-a.wav', '0', '0')
            + path('ag-a.wav', '1.5', '0')
            + path('ag-b.wav', '1', '1')
            + path('ag-a.wav', '1', '-1.25'),
            stereo,
            0.75 + 0.375,
        ),
    }
    for name, (text, source, peak) in cases.items():
        cfg = d / f'ag-{name}.cfg'
        cfg.write_text(text)
        p = call(['--filter', cfg, '--input', source, '--output', dst, '--gain-db', 'no-clip'])
        err = p.stderr.decode()
        db = float(re.search(r'no-clip attenuation (-?[0-9.]+) dB', err).group(1))
        want = -20 * math.log10(peak) if peak else 0.0
        assert abs(db - want) < 0.005, (name, err)
        _, _, gained = floats(dst)
        call(['--filter', cfg, '--input', source, '--output', dst, '--gain-db', f'{want:.6f}'])
        _, _, plain = floats(dst)
        near(gained, plain)
    # ROBUST-11: a filter whose taps are all zero plays as silence: a warning,
    # and no figure from either mode, so the gain stays 0 dB.
    (d / 'ag-silent.cfg').write_text('48000 1 1 0\n0\n0\n' + path('ag-zero.wav', '0', '0'))
    for mode in ('no-clip', 'auto'):
        err = call(['--filter', d / 'ag-silent.cfg', '--input', src, '--output', dst, '--gain-db', mode]).stderr
        assert b'ag-silent.cfg plays as silence' in err and b'attenuation' not in err, (mode, err)
        assert f'--gain-db {mode} gives no figure'.encode() in err and not any(floats(dst)[2]), (mode, err)
    # Coefficients whose bound exceeds the largest float stop before rendering,
    # as auto stops when the noise response overflows.
    huge = d / 'ag-huge.wav'
    float_wave(huge, [(3e38,), (3e38,)])
    for mode in ('no-clip', 'auto'):
        p = call(['--filter', huge, '--input', src, '--output', dst, '--gain-db', mode], ok=False)
        assert b'single-precision' in p.stderr, (mode, p.stderr)


def no_clip_headroom_tests(d):
    """GAIN-3: for integer output no-clip also reserves the quantizer's headroom.

    The input that reaches the filter's bound (+-1, alternating, against
    alternating taps) must stay within +-(1 - H / 2^(B-1)), H = (1 + S)(1/2 + D)
    + 1/2 steps, where native.io shows the quantizer cannot clip; float output
    keeps the bound alone. The same render as float, at the reported gain,
    stays within that range too, and the reported figure, given back to
    --gain-db with the same dither seed, reproduces every sample exactly.
    """
    taps = [12000, -9000, 5000, -2000, 700]
    ir = d / 'nc-ir.wav'
    pcm(ir, taps)
    bound = sum(abs(t) for t in taps) / 32768
    src = d / 'nc-in.wav'
    float_wave(src, [((-1.0) ** i,) for i in range(2000)])
    shaper_sums = {'none': 0.0, 'simple': 1.0, 'third-order': 1.329 + 0.735 + 0.0646}
    shaper_sums['ninth-order'] = 2.203 + 3.210 + 3.970 + 4.404 + 3.668 + 2.656 + 1.644 + 0.767 + 0.118
    cases = [
        ('wav-float', [], None, 0.0, 'none'),
        ('wav-pcm16', [], 16, 1.0, 'none'),  # Triangular dither by default below 20 bits.
        ('wav-pcm16', ['--noise-shaping', 'ninth-order'], 16, 1.0, 'ninth-order'),
        ('wav-pcm8', ['--noise-shaping', 'third-order'], 8, 1.0, 'third-order'),
        ('wav-pcm8', ['--dither', 'none'], 8, 0.0, 'none'),
        ('wav-pcm8', ['--dither', 'triangular-highpass'], 8, 1.0, 'none'),
        ('wav-pcm16', ['--dither', 'none', '--noise-shaping', 'simple'], 16, 0.0, 'simple'),
        ('wav-pcm24', [], 24, 0.0, 'none'),
        ('wav-pcm24', ['--valid-bits', '18', '--noise-shaping', 'simple'], 18, 1.0, 'simple'),
        ('wav-pcm32', [], 32, 0.0, 'none'),
    ]
    for i, (fmt, extra, bits, dither, shaper) in enumerate(cases):
        a, b = d / f'nc-{i}a.wav', d / f'nc-{i}b.wav'
        seed = ['--dither-seed', '7'] if dither else []
        args = ['--filter', ir, '--input', src, '--output-format', fmt, *extra, *seed]
        err = call([*args, '--output', a, '--gain-db', 'no-clip']).stderr.decode()
        text = re.search(r'no-clip attenuation (-?[0-9.]+) dB', err).group(1)
        db = float(text)
        gain = struct.unpack('<f', struct.pack('<f', 10 ** (db / 20)))[0]
        if bits is None:
            assert abs(db + 20 * math.log10(bound)) < 1e-12, err
            assert 'headroom' not in err, err
        else:
            steps = (1 + shaper_sums[shaper]) * (0.5 + dither) + 0.5
            limit = 1 - steps / 2 ** (bits - 1)
            assert bound * gain <= limit, (fmt, extra, bound * gain, limit)
            # The only extra attenuation is that headroom and the 2^-16
            # allowance for float rounding.
            want = -20 * math.log10(bound) + 20 * math.log10(limit / (1 + 2**-16))
            assert abs(db - want) < 1e-9, (fmt, extra, db, want)
            assert f'headroom for {bits}-bit output' in err, err
            assert ('dither' in err.split('headroom')[1]) == (dither > 0), err
            # The engine's own rounding: the same render as float, at the
            # printed gain, stays within the range the quantizer cannot clip.
            f = d / f'nc-{i}f.wav'
            call(['--filter', ir, '--input', src, '--output', f, '--gain-db', text])
            peak = max(abs(v) for v in floats(f)[2])
            assert bound * gain * (1 - 2**-20) < peak <= limit, (fmt, extra, peak, limit)
        call([*args, '--output', b, '--gain-db', text])
        same_audio(a, b, (fmt, extra, text))
    # A numeric gain is applied as given, with no headroom.
    err = call(
        ['--filter', ir, '--input', src, '--output', d / 'nc-n.wav', '--output-format', 'wav-pcm16', '--gain-db', '-1']
    ).stderr
    assert b'headroom' not in err, err


def long_path_tests(d):
    """IO-13: on Windows, an output path over its length limit is refused with advice that names
    its length rather than the folder's permissions: the tools are not long-path aware."""
    if sys.platform != 'win32':
        return
    ir = d / 'long-ir.wav'
    pcm(ir, [16384])
    src = d / 'long-in.wav'
    pcm(src, [1000, -1000])
    # Over 259 characters whatever the temporary folder, and under NTFS's 255 for the name.
    out = d / ('o' * max(240, 270 - len(str(d))) + '.wav')
    p = subprocess.run([EXE, '--filter', ir, '--input', src, '--output', out], capture_output=True, check=False)
    message = p.stderr.decode(errors='replace')
    assert p.returncode != 0, 'a path over 259 characters was written'
    assert f'Its full path is {len(str(out))} characters long' in message, message
    assert 'shorter' in message and 'writable' not in message, message


def stderr_text(stderr):
    """Standard error as text with LF line ends (Windows writes CRLF)."""
    return stderr.decode().replace('\r\n', '\n')


def through_fd(source, args, *, ok=True, env=None):
    """Run with `source`'s bytes on a pipe whose read end is /dev/fd/N in `args`;
    return the standard error."""
    read_end, write_end = os.pipe()
    try:
        argv = [EXE, *(str(a).replace('{fd}', f'/dev/fd/{read_end}') for a in args)]
        with subprocess.Popen(
            argv, pass_fds=(read_end,), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env
        ) as p:
            os.close(read_end)
            read_end = None
            # The child reads while this blocks; if it stops reading and
            # exits, the write fails and its error is reported below.
            pipe = os.fdopen(write_end, 'wb')
            write_end = None
            try:
                with pipe:
                    pipe.write(source.read_bytes())
            except BrokenPipeError:
                pass
            errors = p.communicate(timeout=60)[1]
        assert (p.returncode == 0) == ok, (args, p.returncode, errors.decode(errors='replace'))
        return errors
    finally:
        for fd in (read_end, write_end):
            if fd is not None:
                os.close(fd)


def piped_path_tests(d):
    """IO-16: a pipe named by path (/dev/stdin, or /dev/fd/N as the shell's
    <(command) gives it) reads as the file would, for the input and for the
    filter, while a path naming the output's own file is still refused."""
    if sys.platform == 'win32':
        return
    ir, src, reference = d / 'piped-ir.wav', d / 'piped-in.wav', d / 'piped-reference.wav'
    pcm(ir, [16384, -8192, 4096])
    pcm(src, [((i * 379) % 12001) - 6000 for i in range(4800)])
    call(['--filter', ir, '--input', src, '--output', reference])
    expected = floats(reference)

    out = d / 'piped-out.wav'
    call(['--filter', ir, '--input', '/dev/stdin', '--output', out], data=src.read_bytes())
    assert floats(out) == expected, 'a WAV piped to /dev/stdin must filter as the file does'
    out.unlink()
    through_fd(src, ['--filter', ir, '--input', '{fd}', '--output', out])
    assert floats(out) == expected, 'a WAV on /dev/fd/N must filter as the file does'
    out.unlink()
    through_fd(ir, ['--filter', '{fd}', '--input', src, '--output', out])
    assert floats(out) == expected, 'an IR on /dev/fd/N must filter as the file does'
    # A path that leads to the output's own file is still refused, untouched.
    victim = d / 'piped-victim.wav'
    victim.write_bytes(src.read_bytes())
    with victim.open('rb') as redirected:
        p = subprocess.run(
            [EXE, '--filter', str(ir), '--input', '/dev/stdin', '--output', str(victim)],
            stdin=redirected,
            capture_output=True,
            check=False,
        )
    assert p.returncode != 0 and b'the output is the same file as the input' in p.stderr, p.stderr
    assert victim.read_bytes() == src.read_bytes(), '/dev/stdin naming the output must not overwrite it'
    with victim.open('rb') as opened:
        descriptor = opened.fileno()
        p = subprocess.run(
            [EXE, '--filter', str(ir), '--input', f'/dev/fd/{descriptor}', '--output', str(victim)],
            pass_fds=(descriptor,),
            capture_output=True,
            check=False,
        )
    assert p.returncode != 0 and b'the output is the same file as the input' in p.stderr, p.stderr
    assert victim.read_bytes() == src.read_bytes(), '/dev/fd/N naming the output must not overwrite it'


def piped_filter_tests(d):
    """IO-17: a filter on a pipe is read whole before it is loaded, so a .cfg,
    a list, a FLAC IR and an IR that plays on each channel or as true stereo
    load from a pipe as from the file, and the copy is gone afterwards.
    Relative names in a configuration from a pipe (which are looked for
    beside the copy), a copy that cannot be made, and a piped input libsndfile
    cannot read from a pipe say that the pipe is the reason, naming the pipe,
    not the copy. A file redirected to /dev/stdin reads as the file, and a
    configuration so finds its IR files beside it (macOS shares the
    descriptor's read position; Linux resolved names beside /dev)."""
    if sys.platform == 'win32':
        return
    ir, src = d / 'pf-ir.wav', d / 'pf-in.wav'
    pcm(ir, [16384, -8192, 4096])
    pcm(src, [((i * 379) % 12001) - 6000 if i % 2 else 3000 - (i % 7) * 900 for i in range(9600)], channels=2)
    temporary = d / 'pf-tmp'
    temporary.mkdir()
    env = {**os.environ, 'TMPDIR': str(temporary)}

    def same_as_file(source, filter_args, label):
        reference, out = d / 'pf-reference.wav', d / 'pf-out.wav'
        call([*filter_args(source), '--input', src, '--output', reference])
        through_fd(source, [*filter_args('{fd}'), '--input', src, '--output', out], env=env)
        assert floats(out) == floats(reference), f'{label} from a pipe must filter as the file does'
        assert not any(temporary.iterdir()), f'the copy of {label} from a pipe must be removed'

    # A mono IR on a stereo input plays on each channel, which reads it twice.
    same_as_file(ir, lambda f: ['--filter', f], 'a mono IR on a stereo input')
    four = d / 'pf-four.wav'
    pcm(four, [16384, 0, 0, 8192, -4096, 2048, 1024, 0], channels=4)
    same_as_file(four, lambda f: ['--filter', f], 'a four-channel IR played as true stereo')
    absolute = d / 'pf-absolute.cfg'
    absolute.write_text(f'48000 2 2 0\n0 0\n0 0\n{ir.resolve()}\n0\n0\n0\n{ir.resolve()}\n0\n1\n1\n')
    same_as_file(absolute, lambda f: ['--filter', f], 'a .cfg naming its IR by full path')
    listed = d / 'pf-list.txt'
    listed.write_text(f'{absolute.resolve()}\n')
    same_as_file(listed, lambda f: ['--filter', f], 'a configuration list')
    flac = b'(FLAC output available)' in call(['--version']).stdout
    if flac:
        flac_ir = d / 'pf-ir.flac'
        call(['--filter', ir, '--input', ir, '--output', flac_ir, '--output-format', 'flac-pcm16'])
        same_as_file(flac_ir, lambda f: ['--filter', f], 'a FLAC IR')
        # A FLAC input cannot be read from a pipe; the message says so.
        call(['--filter', ir, '--input', src, '--output', d / 'pf-in.flac', '--output-format', 'flac-pcm16'])
        errors = through_fd(
            d / 'pf-in.flac', ['--filter', ir, '--input', '{fd}', '--output', d / 'pf-out.wav'], ok=False
        )
        assert b'It comes from a pipe' in errors and b'-f wav -' in errors, errors
    # A relative IR name has no folder to be found in; the message says why.
    relative = d / 'pf-relative.cfg'
    relative.write_text('48000 1 1 0\n0\n0\npf-ir.wav\n0\n0\n0\n')
    errors = through_fd(relative, ['--filter', '{fd}', '--input', src, '--output', d / 'pf-out.wav'], ok=False, env=env)
    assert b'came from a pipe' in errors and b'full paths' in errors, errors
    assert str(temporary).encode() not in errors, 'the message must name the pipe, not its copy'
    assert not any(temporary.iterdir()), 'the copy of a refused filter from a pipe must be removed'
    # So does a named pipe given by a relative path, whose folder is not searched.
    fifo = d / 'pf-fifo'
    os.mkfifo(fifo)
    writer = subprocess.Popen(['sh', '-c', 'cat "$1" > "$2"', 'sh', str(relative), str(fifo)])
    p = subprocess.run(
        [EXE, '--filter', 'pf-fifo', '--input', str(src), '--output', str(d / 'pf-out.wav')],
        cwd=d,
        capture_output=True,
        env=env,
        check=False,
        timeout=60,
    )
    writer.wait(timeout=60)
    assert p.returncode != 0 and p.stderr.startswith(b'convolver: error: pf-fifo:4:1: '), p.stderr
    assert b'came from a pipe' in p.stderr and str(temporary).encode() not in p.stderr, p.stderr
    # A copy that cannot be made says why, and leaves nothing behind.
    errors = through_fd(
        ir,
        ['--filter', '{fd}', '--input', src, '--output', d / 'pf-out.wav'],
        ok=False,
        env={**os.environ, 'TMPDIR': str(d / 'pf-missing')},
    )
    assert b'cannot copy the filter from /dev/fd/' in errors and b'TMPDIR' in errors, errors
    # --inspect opens a filter more than once: an IR file reads from a pipe,
    # and a configuration that cannot says the pipe may be why.
    through_fd(ir, ['--inspect', '{fd}'])
    errors = through_fd(absolute, ['--inspect', '{fd}'], ok=False)
    assert b'is a pipe, which --inspect and --validate can read only' in errors, errors
    # A file redirected to /dev/stdin, as the input or as the filter.
    (d / 'pf-stereo.cfg').write_text('48000 2 2 0\n0 0\n0 0\npf-ir.wav\n0\n0\n0\npf-ir.wav\n0\n1\n1\n')
    reference, out = d / 'pf-reference.wav', d / 'pf-out.wav'
    call(['--filter', ir, '--input', src, '--output', reference])
    # A configuration redirected so finds its IR files beside its own file.
    for args, redirected in (
        (['--filter', ir, '--input', '/dev/stdin'], src),
        (['--filter', '/dev/stdin', '--input', src], ir),
        (['--filter', '/dev/stdin', '--input', src], d / 'pf-stereo.cfg'),
    ):
        out.unlink(missing_ok=True)
        with redirected.open('rb') as stdin:
            p = subprocess.run(
                [EXE, *map(str, args), '--output', str(out)], stdin=stdin, capture_output=True, check=False
            )
        assert p.returncode == 0, (args, p.stderr.decode(errors='replace'))
        assert floats(out) == floats(reference), (
            f'{redirected.name} redirected to /dev/stdin must filter as the file does'
        )
    # IO-23: --compare reads a file redirected to /dev/stdin as the file (on
    # macOS the descriptor's shared position made a valid WAV unreadable).
    with reference.open('rb') as stdin:
        p = subprocess.run(
            [EXE, '--compare', '/dev/stdin', str(reference)], stdin=stdin, capture_output=True, check=False
        )
    assert p.returncode == 0, p.stderr.decode(errors='replace')


def clip_report_tests(d):
    """DIAG-5: after a render, stderr reports what went beyond full scale.

    Integer output gives each affected channel's clipped-sample count, the
    quantizer's own, and its peak in dBFS, and suggests --gain-db no-clip;
    float output, which keeps such samples, gets a note instead. A clean
    render says nothing, and the exit status and samples are unchanged.
    """
    ir = d / 'clip-ir.wav'
    pcm(ir, [16384, 0], channels=2)  # 0.5 on the left, silence on the right.
    (d / 'clip.cfg').write_text('48000 2 2 0\n0 0\n0 0\nclip-ir.wav\n0\n0\n0\nclip-ir.wav\n1\n1\n1\n')
    cfg = d / 'clip.cfg'
    src = d / 'clip-in.wav'
    float_wave(src, [(0.9 * math.sin(i / 7), 0.9) for i in range(4800)], channels=2)
    real, fixed = d / 'clip-float.wav', d / 'clip-pcm24.wav'
    # +12 dB on a peak of 0.45 reaches about 1.8 (+5.1 dBFS) on the left;
    # the right channel is silent.
    p = call(['--filter', cfg, '--input', src, '--output', real, '--gain-db', '12'])
    _, _, values = floats(real)
    left = values[0::2]
    peak = max(abs(v) for v in left)
    above = sum(abs(v) > 1 for v in left)
    peak_text = f'peak {20 * math.log10(peak):+.2f} dBFS'
    err = stderr_text(p.stderr)
    assert f'convolver: note: {real} has {above} samples above full scale in 1 of 2 channels' in err, err
    assert f'channel 1: {above} of {len(left)} samples above full scale, {peak_text}' in err, err
    assert 'channel 2' not in err and 'converting it to integer samples later' in err, err
    assert 'samples clipped' not in err and '--gain-db no-clip' in err, err
    # The same render to 24-bit integers clips exactly the samples that round
    # beyond the range (no dither at 24 bits), and the file holds the clipped
    # values: the report changes nothing else.
    p = call(['--filter', cfg, '--input', src, '--output', fixed, '--gain-db', '12', '--output-format', 'wav-pcm24'])
    clipped = sum(not -(2**23) <= math.floor(v * 2**23 + 0.5) <= 2**23 - 1 for v in left)
    assert clipped > 1000, clipped
    err = stderr_text(p.stderr)
    assert f'convolver: warning: {fixed} clipped {clipped} samples in 1 of 2 channels' in err, err
    assert f'channel 1: {clipped} of {len(left)} samples clipped, {peak_text}' in err, err
    assert '24-bit integer samples cannot hold them' in err and 'channel 2' not in err, err
    assert f'lower --gain-db by more than the highest peak, {20 * math.log10(peak):.2f} dB' in err, err
    assert 'Render again with --gain-db no-clip' in err, err
    _, samples, _ = wav_audio(fixed)
    want = [max(-(2**23), min(2**23 - 1, math.floor(v * 2**23 + 0.5))) for v in values]
    assert list(samples) == want
    # To standard output, by the same name the failure messages use.
    p = call(['--filter', cfg, '--input', src, '--output', '-', '--output-format', 'raw-pcm16', '--gain-db', '12'])
    assert b'convolver: warning: standard output clipped' in p.stderr, p.stderr
    # Within full scale, nothing is said, whatever the sample type.
    for fmt in ('wav-float', 'wav-pcm24', 'wav-pcm16'):
        p = call(['--filter', cfg, '--input', src, '--output', fixed, '--output-format', fmt])
        assert b'full scale' not in p.stderr and b'samples clipped' not in p.stderr, (fmt, p.stderr)
    # Mono output names no channel count.
    mono_ir, mono_in = d / 'clip-mono-ir.wav', d / 'clip-mono-in.wav'
    pcm(mono_ir, [16384])
    float_wave(mono_in, [(0.9,), (-0.9,)])
    p = call(
        ['--filter', mono_ir, '--input', mono_in, '--output', fixed, '--output-format', 'wav-pcm16', '--gain-db', '12']
    )
    assert f'{fixed} clipped 2 samples\n    channel 1: 2 of 2 samples clipped' in stderr_text(p.stderr), p.stderr
    # Only negative samples clipped: written as the nearest value, not the largest.
    float_wave(mono_in, [(-0.9,), (0.1,)])
    args = ['--input', mono_in, '--output', fixed, '--output-format', 'wav-pcm16', '--dither', 'none']
    err = stderr_text(call(['--filter', mono_ir, *args, '--gain-db', '12']).stderr)
    assert 'clipped 1 sample\n' in err and 'written as the nearest value they can hold' in err, err
    assert 'is too high' in err and 'largest value' not in err, err
    # Rounding alone: +1.0 rounds one step past the largest positive value,
    # so the cause names rounding rather than the gain.
    unit_ir = d / 'clip-unit-ir.wav'
    float_wave(unit_ir, [(1.0,)])
    float_wave(mono_in, [(1.0,), (0.5,)])
    err = stderr_text(call(['--filter', unit_ir, *args]).stderr)
    assert 'clipped 1 sample\n    channel 1: 1 of 2 samples clipped, peak +0.00 dBFS' in err, err
    assert 'Those samples were within full scale, but rounding took them just beyond' in err, err
    assert 'leaves room for rounding' in err and 'is too high' not in err, err
    # DIAG-8: dither can take a +1.0 sample down within range, so the cause
    # says why to leave headroom rather than that +1.0 always clips.
    assert '+1.0 is above the largest representable positive integer sample' in err, err
    assert 'always clips' not in err, err
    # With triangular dither most, but not all, +1.0 samples clip (7 in 8 on
    # average), and the count says so.
    float_wave(mono_in, [(1.0,)] * 4096)
    dithered = ['--input', mono_in, '--output', fixed, '--output-format', 'wav-pcm16', '--dither', 'triangular']
    err = stderr_text(call(['--filter', unit_ir, *dithered]).stderr)
    count = re.search(r'channel 1: (\d+) of 4096 samples clipped', err)
    assert count and 3000 < int(count.group(1)) < 4096, err
    assert '+1.0 is above the largest representable positive integer sample' in err, err
    # With no-clip only input beyond full scale can clip, so the fix says so.
    loud = d / 'clip-loud.wav'
    float_wave(loud, [(1.5 * math.sin(i / 5), 0.1) for i in range(1000)], channels=2)
    p = call(
        ['--filter', cfg, '--input', loud, '--output', fixed, '--output-format', 'wav-pcm24', '--gain-db', 'no-clip']
    )
    err = stderr_text(p.stderr)
    assert 'clipped' in err and 'Lower --gain-db below the no-clip figure reported above' in err, err
    assert 'this input goes beyond full scale' in err and 'Render again' not in err, err
    # ROBUST-10: float output with no-clip keeps the bound alone, so up to
    # 2^-16 over full scale can be the convolution's rounding and is not put
    # down to the input; more than that is. One tap of 1.0 adds no rounding,
    # so the output is the input exactly.
    for level, rounding in ((1 + 2**-17, True), (1 + 2**-15, False)):
        float_wave(mono_in, [(level,), (0.5,)])
        args = ['--filter', unit_ir, '--input', mono_in, '--output', real, '--gain-db', 'no-clip']
        err = stderr_text(call(args).stderr)
        assert 'sample above full scale' in err, (level, err)
        assert ('need not mean that the input went beyond full scale' in err) == rounding, (level, err)
        assert ('0.001 dB more attenuation is enough' in err) == rounding, (level, err)
        assert ('this input goes beyond full scale' in err) != rounding, (level, err)


def output_selection_tests(d):
    """--output-channels selects list entries by output count, as the historical engine did."""
    ir = d / 'os-ir.wav'
    src = d / 'os-in.wav'
    dst = d / 'os-out.wav'
    pcm(ir, [16384])
    pcm(src, [1000, -2000, 3000])
    (d / 'os-wide.cfg').write_text('48000 1 3 0\n0\n0 0 0\nos-ir.wav\n0\n0\n0 1 2\n')
    (d / 'os-narrow.cfg').write_text('48000 1 1 0\n0\n0\nos-ir.wav\n0\n0\n0\n')
    (d / 'os-list.txt').write_text('os-wide.cfg\nos-narrow.cfg\n')
    base = ['--filter', d / 'os-list.txt', '--input', src, '--output', dst]
    call(base)
    assert floats(dst)[0] == 3
    call([*base, '--output-channels', '1'])
    assert floats(dst)[0] == 1
    call([*base, '--output-channels', '3'])
    assert floats(dst)[0] == 3
    p = call([*base, '--output-channels', '2'], ok=False)
    assert b'output channel count' in p.stderr, p.stderr


def repeatability_tests(d):
    """Separate runs on the same input must write bit-identical samples.

    Each run is a new process, so its heap layout differs. Output must not depend
    on where buffers land, or exact comparisons between renders become flaky.
    """
    ir = d / 'rep-ir.wav'
    src = d / 'rep-in.wav'
    out = d / 'rep-out.wav'
    pcm(ir, [(i * 7919) % 20001 - 10000 for i in range(300)])
    pcm(src, [(i * 104729) % 30001 - 15000 for i in range(2 * 3000)], channels=2)
    cfg = d / 'rep.cfg'
    cfg.write_text(
        '48000 2 6 3f\n0 0\n0 0 0 0 0 0\n' + ''.join(f'rep-ir.wav\n0\n{o % 2}\n{o}.{o + 1}\n' for o in range(6))
    )
    first = None
    for run in range(20):
        call(['--filter', cfg, '--input', src, '--output', out, '--output-format', 'wav-float'])
        samples = floats(out)[2]
        if first is None:
            first = samples
        differing = sum(a != b for a, b in zip(samples, first))
        assert samples == first, f'run {run} differs from run 0 in {differing} of {len(first)} samples'


def speaker_map_tests(d):
    """--speaker-map config labels WAV channels with the configuration's mask."""
    ir = d / 'sm-ir.wav'
    src = d / 'sm-in.wav'
    out = d / 'sm-out.wav'
    pcm(ir, [16384])
    pcm(src, [8192, -4096, 2048, -1024, 4096, 1024], channels=2)

    def config(mask, outputs):
        # Input 0 feeds output o with weight 0.(o+1), so each channel is distinguishable.
        # One file per mask, so a list of configurations made at once keeps each.
        cfg = d / f'sm-{mask:x}-{outputs}.cfg'
        cfg.write_text(
            f'48000 2 {outputs} {mask:x}\n0 0\n{" ".join("0" * outputs)}\n'
            + ''.join(f'sm-ir.wav\n0\n0\n{o}.{o + 1}\n' for o in range(outputs))
        )
        return cfg

    for mask, outputs, fmt in (
        (0x3, 2, 'wav-float'),
        (0x3F, 6, 'wav-pcm24'),
        (0x3F, 6, 'wav-float'),
        (0x60F, 6, 'rf64-pcm24'),  # IO-5: RF64 keeps the exact mask too.
    ):
        cfg = config(mask, outputs)
        base = ['--filter', cfg, '--input', src, '--output', out, '--output-format', fmt]
        call(base)
        plain = out.read_bytes()
        # libsndfile gives unlabelled RF64 its default mask for the channel count.
        assert channel_mask(out) is None or fmt.startswith('rf64'), hex(channel_mask(out) or 0)
        call([*base, '--speaker-map', 'config'])
        assert channel_mask(out) == mask, hex(channel_mask(out) or 0)
        if fmt == 'wav-float':
            ch, _rate, labelled = floats(out)
            assert ch == outputs
            out.write_bytes(plain)
            assert floats(out)[2] == labelled, 'labels must not reorder samples'
    # Zero mask: an unlabelled file and a note; mismatched masks and RAW fail before output.
    cfg = config(0, 2)
    p = call(['--filter', cfg, '--input', src, '--output', out, '--speaker-map', 'config'])
    assert channel_mask(out) is None and b'no speaker mask' in p.stderr
    for args in (
        [config(0x7, 2)],
        [config(0x3, 2), '--output-format', 'raw-float'],
        [config(0x40001, 2)],
        [config(0x3, 2), '--output-format', 'caf-pcm24'],
        [config(0x3, 2), '--output-format', 'w64-float'],
    ):
        out.unlink(missing_ok=True)
        p = call(['--filter', args[0], '--input', src, '--output', out, '--speaker-map', 'config', *args[1:]], ok=False)
        assert not out.exists() and b'speaker' in p.stderr, p.stderr
    call(['--filter', config(0x3, 2), '--input', src, '--output', out, '--speaker-map', 'wave'], ok=False)


def container_tests(d):
    """--output-format CONTAINER-TYPE and --raw-format, end to end."""
    ir = d / 'ct-ir.wav'
    pcm(ir, [16384])
    raw = struct.pack('<4f', 0.5, -0.25, 0.125, -1.0)
    base = ['--filter', ir, '--raw-input', '48000', '1', '--input', '-']
    magic = {'wav': b'RIFF', 'rf64': b'RF64', 'w64': b'riff', 'aiff': b'FORM', 'caf': b'caff', 'flac': b'fLaC'}
    # Preview packages build libsndfile without FLAC; --version says which.
    flac = b'(FLAC output available)' in call(['--version']).stdout
    for name in (
        'wav-double',
        'rf64-pcm24',
        'w64-pcm8',
        'aiff-pcm16',
        'aiff-float',
        'caf-pcm32',
        'caf-double',
        'flac-pcm8',
        'flac-pcm16',
        'flac-pcm24',
    ):
        out = d / ('ct.' + name.split('-')[0])
        out.unlink(missing_ok=True)
        if name.startswith('flac') and not flac:
            p = call([*base, '--output', out, '--output-format', name], ok=False, data=raw)
            assert b'without FLAC support' in p.stderr and not out.exists(), p.stderr
            continue
        call([*base, '--output', out, '--output-format', name], data=raw)
        assert out.read_bytes()[:4] == magic[name.split('-')[0]], name
    # The original format names still write the same files as before.
    for name, subtype, size in (
        ('wav-pcm8', 1, 1),
        ('wav-pcm16', 1, 2),
        ('wav-pcm24', 1, 3),
        ('wav-pcm32', 1, 4),
        ('wav-float', 3, 4),
    ):
        out = d / 'legacy.wav'
        call([*base, '--output', out, '--output-format', name], data=raw)
        b = out.read_bytes()
        assert b[:4] == b'RIFF' and b[8:16] == b'WAVEfmt ', name
        assert struct.unpack('<HHIIHH', b[20:36]) == (subtype, 1, 48000, 48000 * size, size, 8 * size), name
    # Integer and double raw output, read back with the matching --raw-format.
    for name, fmt, size in (
        ('raw-pcm16', 's16le', 2),
        ('raw-pcm24', 's24le', 3),
        ('raw-pcm32', 's32le', 4),
        ('raw-double', 'f64le', 8),
    ):
        plain = ['--dither', 'none'] if name == 'raw-pcm16' else []
        p = call([*base, '--output', '-', '--output-format', name, *plain], data=raw)
        assert len(p.stdout) == 4 * size, (name, len(p.stdout))
        back = call(
            [
                '--filter',
                ir,
                '--raw-input',
                '48000',
                '1',
                '--raw-format',
                fmt,
                '--input',
                '-',
                '--output',
                '-',
                '--output-format',
                'raw-float',
            ],
            data=p.stdout,
        )
        near(struct.unpack('<4f', back.stdout), [0.125, -0.0625, 0.03125, -0.25])  # filtered twice
    assert (
        call([*base, '--output', '-', '--output-format', 'raw-pcm16', '--dither', 'none'], data=raw).stdout[:2]
        == b'\0\x20'
    )
    # s16 and s24 files whose size is not a multiple of 4 bytes keep every frame,
    # across a read-chunk boundary, from a file and from redirected stdin.
    for fmt, width in (('s16le', 2), ('s24le', 3)):
        n = 4097
        f = d / ('odd.' + fmt)
        f.write_bytes(
            b''.join(((i % 7 - 3) << (8 * width - 4)).to_bytes(width, 'little', signed=True) for i in range(n))
        )
        args = [
            '--filter',
            ir,
            '--raw-input',
            '48000',
            '1',
            '--raw-format',
            fmt,
            '--output',
            '-',
            '--output-format',
            'raw-float',
        ]
        p = call([*args, '--input', f])
        assert len(p.stdout) == 4 * n, (fmt, len(p.stdout))
        with Path(f).open('rb') as seekable:  # stdin redirected from a file
            p = subprocess.run([EXE, *map(str, args), '--input', '-'], stdin=seekable, capture_output=True, check=False)
        assert p.returncode == 0 and len(p.stdout) == 4 * n, (fmt, p.returncode, len(p.stdout))
        call([*args, '--input', '-'], data=f.read_bytes() + b'\0', ok=False)  # incomplete frame
    # Big-endian input in each type.
    for fmt, packed in (
        ('s16be', struct.pack('>4h', 16384, -8192, 4096, -32768)),
        ('s32be', struct.pack('>4i', 1 << 30, -(1 << 29), 1 << 28, -(1 << 31))),
        ('f32be', struct.pack('>4f', 0.5, -0.25, 0.125, -1.0)),
        ('f64be', struct.pack('>4d', 0.5, -0.25, 0.125, -1.0)),
        ('s24be', b''.join(v.to_bytes(3, 'big', signed=True) for v in (1 << 22, -(1 << 21), 1 << 20, -(1 << 23)))),
    ):
        p = call(
            [
                '--filter',
                ir,
                '--raw-input',
                '48000',
                '1',
                '--raw-format',
                fmt,
                '--input',
                '-',
                '--output',
                '-',
                '--output-format',
                'raw-float',
            ],
            data=packed,
        )
        near(struct.unpack('<4f', p.stdout), [0.25, -0.125, 0.0625, -0.5])
    out = d / 'ct.flac'
    for bad in (
        ['--output-format', 'flac-float'],
        ['--output-format', 'raw-pcm8'],
        ['--output-format', 'flac-pcm32'],
        ['--output-format', 'ogg-pcm16'],
        ['--output-format', 'wav'],
        ['--raw-format', 'u8'],
        ['--raw-format', 's16'],
        ['--output-format', 'flac-pcm24', '--speaker-map', 'config'],
        ['--output-format', 'aiff-pcm24', '--valid-bits', '20'],
    ):
        out.unlink(missing_ok=True)
        call([*base, '--output', out, *bad], ok=False, data=raw)
        assert not out.exists(), bad
    call(['--filter', ir, '--input', d / 'ct.aiff', '--raw-format', 's16le', '--output', out], ok=False)
    # A non-raw stream on stdout names the raw formats.
    p = call([*base, '--output', '-', '--output-format', 'aiff-pcm16'], ok=False, data=raw)
    assert b'raw output format' in p.stderr, p.stderr
    # Without FLAC, a FLAC input or IR says what is missing and what to do.
    if b'no FLAC output' in call(['--version']).stdout:
        fake = d / 'ct-fake.flac'
        fake.write_bytes(b'fLaC' + bytes(64))
        p = call(['--filter', ir, '--input', fake, '--output', d / 'ct-fake.wav'], ok=False)
        assert b'built without FLAC' in p.stderr and b'flac --decode' in p.stderr, p.stderr
        ogg = d / 'ct-fake.ogg'
        ogg.write_bytes(b'OggS' + bytes(64))
        p = call(['--filter', ir, '--input', ogg, '--output', d / 'ct-fake.wav'], ok=False)
        assert b'an Ogg file' in p.stderr and b'oggdec' in p.stderr, p.stderr
        assert b'flac --decode' not in p.stderr, p.stderr
        (d / 'ct-fake.cfg').write_text('48000 1 1 0\n0\n0\nct-fake.flac\n0\n0\n0\n')
        p = call(['--filter', d / 'ct-fake.cfg', '--input', d / 'ct.aiff', '--output', d / 'ct-fake.wav'], ok=False)
        assert b'built without FLAC' in p.stderr and b'looked for' not in p.stderr, p.stderr
        # A named pipe is never reopened to sniff it: that would block forever.
        if hasattr(os, 'mkfifo'):
            fifo = d / 'ct-fifo'
            os.mkfifo(fifo)
            writer = subprocess.Popen(['sh', '-c', 'head -c 200 /dev/zero > "$1"', 'sh', str(fifo)])
            p = subprocess.run(
                [EXE, '--filter', str(ir), '--input', str(fifo), '--output', str(d / 'ct-fifo.wav')],
                capture_output=True,
                timeout=20,
                check=False,
            )
            writer.wait(timeout=20)
            assert p.returncode != 0 and b'built without FLAC' not in p.stderr, p.stderr
    # A DRC-style .pcm filter in a configuration.
    (d / 'ct.pcm').write_bytes(struct.pack('<2f', 0.5, 0.25))
    cfg = d / 'ct-raw.cfg'
    cfg.write_text('48000 1 1 0\n0\n0\nct.pcm\n0\n0\n0\n')
    p = call(
        ['--filter', cfg, '--raw-input', '48000', '1', '--input', '-', '--output', '-', '--output-format', 'raw-float'],
        data=raw,
    )
    near(struct.unpack('<5f', p.stdout), [0.25, 0.0, 0.0, -0.46875, -0.25])
    # The assumed sample type is reported, and --raw-filter-format changes it.
    assert b"raw filter 'ct.pcm' read as mono f32le at 48000 Hz" in p.stderr, p.stderr
    assert b'--raw-filter-format' in p.stderr, p.stderr
    (d / 'ct.pcm').write_bytes(struct.pack('<2h', 16384, 8192))
    p = call(
        [
            '--filter',
            cfg,
            '--raw-filter-format',
            's16le',
            '--raw-input',
            '48000',
            '1',
            '--input',
            '-',
            '--output',
            '-',
            '--output-format',
            'raw-float',
        ],
        data=raw,
    )
    near(struct.unpack('<5f', p.stdout), [0.25, 0.0, 0.0, -0.46875, -0.25])
    assert b'read as mono s16le' in p.stderr, p.stderr
    assert b'choose another sample type' not in p.stderr, p.stderr
    # Two raw filters with the same file name are told apart by their folders.
    for sub in ('ct-a', 'ct-b'):
        (d / sub).mkdir()
        (d / sub / 'x.pcm').write_bytes(struct.pack('<f', 1))
    two = d / 'ct-two.cfg'
    two.write_text('48000 1 1 0\n0\n0\nct-a/x.pcm\n0\n0\n0\nct-b/x.pcm\n0\n0\n0\n')
    p = call(
        ['--filter', two, '--raw-input', '48000', '1', '--input', '-', '--output', '-', '--output-format', 'raw-float'],
        data=raw,
    )
    assert b"'ct-a/x.pcm'" in p.stderr and b"'ct-b/x.pcm'" in p.stderr, p.stderr
    # The option says when there is nothing for it to do.
    p = call(
        ['--filter', ir, '--raw-filter-format', 's24le', '--input', d / 'ct.aiff', '--output', d / 'ct-unused.wav']
    )
    assert b'--raw-filter-format has no effect' in p.stderr, p.stderr
    p = call(
        [
            '--filter',
            cfg,
            '--raw-filter-format',
            's16',
            '--raw-input',
            '48000',
            '1',
            '--input',
            '-',
            '--output',
            '-',
            '--output-format',
            'raw-float',
        ],
        ok=False,
        data=raw,
    )
    assert b'unsupported raw filter format' in p.stderr, p.stderr
    # A WAV that happens to be named .pcm is still read through its header.
    pcm(d / 'ct.pcm', [16384, 8192])
    p = call(
        ['--filter', cfg, '--raw-input', '48000', '1', '--input', '-', '--output', '-', '--output-format', 'raw-float'],
        data=raw,
    )
    near(struct.unpack('<5f', p.stdout), [0.25, 0.0, 0.0, -0.46875, -0.25])
    assert b'raw filter' not in p.stderr, p.stderr


def main():
    call(['--help'])
    version = call(['--version']).stdout
    assert re.fullmatch(
        rb'Convolver portable [0-9]+\.[0-9]+\.[0-9]+(?:\+g[0-9a-f]+(?:\.dirty)?)?', version.splitlines()[0]
    ), version
    assert b'FFT backend:' in version
    assert re.search(rb'CPU kernels: (AVX2\+FMA|baseline)', version), version
    with tempfile.TemporaryDirectory(prefix='convolver-cli-') as t:
        d = Path(t)
        src = d / 'entrée.wav'
        ir = d / 'ir.wav'
        dst = d / 'sortie ü.wav'
        for count in (0, 1, 16, 17, 64):
            x = [((i * 379) % 12001) - 6000 for i in range(count)]
            h = [16384, -8192, 4096, 0, 2048, 0, 1024]
            pcm(src, x)
            pcm(ir, h)
            expected = [0.0] * (count + len(h) - 1) if count else []
            for i, v in enumerate(x):
                for j, w in enumerate(h):
                    expected[i + j] += v * w / (32768.0**2)
            base = ['--filter', ir, '--input', src, '--output', dst, '--block-size', '8']
            call(base)
            channels, rate, y = floats(dst)
            assert (channels, rate) == (1, 48000)
            near(y, expected)
            call([*base, '--keep-latency'])
            near(floats(dst)[2], ([0.0] * 8 + expected) if count else [])
        # Delay/routing/negative-zero and relative IR path, distinct I/O counts.
        pcm(ir, [16384, 8192])
        pcm(src, [8192, 4096, -4096, 2048], channels=2)
        cfg = d / 'matrix.cfg'
        cfg.write_text('48000 2 3 0\n0 0.020833333333333333\n0 0 0\nir.wav\n0\n-0 1.5\n0 2.5\n')
        call(['--filter', cfg, '--input', src, '--output', dst, '--block-size', '3'])
        ch, rate, y = floats(dst)
        assert ch == 3
        # Exact delay truncation belongs to parser tests; use a zero-delay config here.
        cfg.write_text('48000 2 3 0\n0 0\n0 0 0\nir.wav\n0\n-0 1.5\n0 2.5\n')
        call(['--filter', cfg, '--input', src, '--output', dst, '--block-size', '3'])
        mixed = [-0.25 + 0.5 * 0.125, 0.125 + 0.5 * 0.0625]
        z = [mixed[0] * 0.5, mixed[1] * 0.5 + mixed[0] * 0.25, mixed[1] * 0.25]
        near(floats(dst)[2], [v for s in z for v in (s, 0, s * 0.5)])
        # True pipes: explicit float32 LE metadata, no seekable header dependency.
        pcm(ir, [16384, 8192])
        raw = struct.pack('<3f', 0.25, -0.5, 0.75)
        args = [
            '--filter',
            ir,
            '--input',
            '-',
            '--raw-input',
            '48000',
            '1',
            '--output',
            '-',
            '--output-format',
            'raw-float',
            '--block-size',
            '2',
        ]
        p = call(args, data=raw)
        near(struct.unpack('<4f', p.stdout), [0.125, -0.1875, 0.25, 0.1875])
        p = call([*args, '--gain-db', '6.020599913279624'], data=raw)
        near(struct.unpack('<4f', p.stdout), [0.25, -0.375, 0.5, 0.375])
        # EOF after full/partial read buffers must retain every frame and tail.
        for count in (0, 1, 4096, 4097):
            payload = struct.pack(f'<{count}f', *([0.25] * count))
            p = call(args, data=payload)
            expected = ([0.125] + [0.1875] * (count - 1) + [0.0625]) if count else []
            near(struct.unpack(f'<{len(p.stdout) // 4}f', p.stdout), expected)
        # Normal pipe EOF must not hide incomplete samples or nonfinite input.
        for payload in (b'\0', raw + b'\0', struct.pack('<f', float('nan'))):
            call(args, ok=False, data=payload)
        # A producer may split float samples across separate writes.
        with subprocess.Popen(
            [EXE, *map(str, args)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        ) as p:
            for byte in raw:
                p.stdin.write(bytes([byte]))
                p.stdin.flush()
            p.stdin.close()
            p.stdin = None
            output, errors = p.communicate(timeout=30)
            assert p.returncode == 0, errors
            near(struct.unpack('<4f', output), [0.125, -0.1875, 0.25, 0.1875])
        # Referenced IRs and nested config/list files must never be overwritten.
        original_ir = ir.read_bytes()
        call(['--filter', cfg, '--input', src, '--output', ir], ok=False)
        assert ir.read_bytes() == original_ir
        listing = d / 'filters.txt'
        listing.write_text('matrix.cfg\n')
        original_cfg = cfg.read_bytes()
        call(['--filter', listing, '--input', src, '--output', cfg], ok=False)
        assert cfg.read_bytes() == original_cfg
        # stdin can be a regular file; opening that same path as output is destructive.
        raw_path = d / 'source.raw'
        raw_path.write_bytes(raw)
        with raw_path.open('rb') as redirected:
            p = subprocess.run(
                [
                    EXE,
                    '--filter',
                    str(ir),
                    '--input',
                    '-',
                    '--raw-input',
                    '48000',
                    '1',
                    '--output',
                    str(raw_path),
                    '--output-format',
                    'raw-float',
                ],
                stdin=redirected,
                capture_output=True,
                check=False,
            )
        assert p.returncode != 0 and raw_path.read_bytes() == raw
        # PCM16 output has TPDF dither by default; --dither none rounds plainly;
        # dither is reproducible from its seed.
        pcm(ir, [13107])
        pcm(src, [((i * 379) % 12001) - 6000 for i in range(4000)])

        def shorts(path):
            with wave.open(str(path), 'rb') as f:
                assert f.getsampwidth() == 2
                return struct.unpack(f'<{f.getnframes()}h', f.readframes(f.getnframes()))

        base = ['--filter', ir, '--input', src, '--output', dst, '--output-format', 'wav-pcm16']
        exact = [(((i * 379) % 12001) - 6000) * 13107 / 32768 for i in range(4000)]
        p = call([*base, '--dither', 'none'])
        plain = shorts(dst)
        assert b'without dither, 16-bit output' in p.stderr, p.stderr
        assert all(a == math.floor(b + 0.5) for a, b in zip(plain, exact) if abs(b % 1 - 0.5) > 0.01)
        p = call(base)
        first = shorts(dst)
        assert b'16-bit output: adding triangular (TPDF) dither' in p.stderr, p.stderr
        assert b'triangular-highpass --noise-shaping ninth-order' in p.stderr, p.stderr
        call([*base, '--dither', 'triangular'])
        assert shorts(dst) == first
        call([*base, '--dither', 'triangular', '--dither-seed', '0'])
        assert shorts(dst) == first and first != plain
        assert all(abs(a - b) <= 1.5 for a, b in zip(first, exact))
        call([*base, '--dither', 'triangular', '--dither-seed', '1'])
        assert shorts(dst) != first
        call([*base, '--dither', 'triangular-highpass'])
        assert all(abs(a - b) <= 1.5 for a, b in zip(shorts(dst), exact))
        # A choice another beats clearly at this rate (48 kHz) points to it;
        # the best choice draws no warning.
        p = call([*base, '--noise-shaping', 'simple'])
        assert b'warning: at 48000 Hz, --dither triangular-highpass --noise-shaping ninth-order' in p.stderr, p.stderr
        assert not call([*base, '--dither', 'triangular-highpass', '--noise-shaping', 'ninth-order']).stderr
        p = call([*base, '--output-format', 'wav-float', '--dither-seed', '4'])
        assert b'--dither-seed has no effect' in p.stderr, p.stderr
        # Options another always beats are refused with the reason and the alternative.
        for removed, why in (
            (['--dither', 'rectangular'], b'noise modulation'),
            (['--noise-shaping', 'second-order'], b'Use third-order'),
            (['--noise-shaping', 'sony-sbm'], b'at least 3 dB less audible'),
        ):
            p = call(base + removed, ok=False)
            assert why in p.stderr, p.stderr
        # Shaping alone still adds the default dither, and says so.
        p = call([*base, '--noise-shaping', 'ninth-order'])
        assert b'adding triangular (TPDF) dither' in p.stderr, p.stderr
        # The advice follows the sample rate: none at 8 kHz, where the default is
        # best; simple shaping with high-pass dither at 96 kHz.
        for rate, advice in (
            (8000, None),
            (96000, b'--dither triangular-highpass --noise-shaping simple would keep the noise about 16 dB'),
        ):
            r_ir = d / 'rate-ir.wav'
            r_src = d / 'rate-in.wav'
            pcm(r_ir, [13107], rate=rate)
            pcm(r_src, [((i * 379) % 12001) - 6000 for i in range(4000)], rate=rate)
            p = call(['--filter', r_ir, '--input', r_src, '--output', dst, '--output-format', 'wav-pcm16'])
            assert b'adding triangular (TPDF) dither' in p.stderr, p.stderr
            assert (advice in p.stderr) if advice else (b'would keep' not in p.stderr), (rate, p.stderr)
        for extra in (
            ['--dither', 'bogus'],
            ['--dither-seed', '-1'],
            ['--dither-seed', 'x'],
            ['--output-format', 'wav-float', '--dither', 'triangular'],
            ['--output-format', 'raw-float', '--dither', 'triangular'],
            ['--noise-shaping', 'bogus'],
            ['--output-format', 'wav-float', '--noise-shaping', 'simple'],
            ['--valid-bits', '7'],
            ['--valid-bits', '17'],
            ['--output-format', 'wav-pcm8', '--valid-bits', '9'],
            ['--output-format', 'wav-float', '--valid-bits', '16'],
        ):
            call(['--filter', ir, '--input', src, '--output', dst, *extra], ok=False)
        call(['--filter', ir, '--input', src, '--output', dst, '--dither', 'none'])
        integer_format_tests(d)
        automatic_gain_tests(d)
        no_clip_headroom_tests(d)
        clip_report_tests(d)
        piped_path_tests(d)
        piped_filter_tests(d)
        output_selection_tests(d)
        # Errors must be failures, not successful silent output.
        for extra in (['--block-size', '1048577'], ['--block-size', '-1'], ['--gain-db', 'nan'], ['--unknown']):
            call(['--filter', ir, '--input', src, '--output', dst, *extra], ok=False)
        # Zero latency (PERF-2) renders the same audio, aligned as usual.
        call(['--filter', ir, '--input', src, '--output', dst, '--block-size', '0'])
        call(['--filter', ir, '--input', src, '--output', src], ok=False)
        call(['--filter', ir, '--input', '-', '--output', dst], ok=False, data=raw)
        call(['--filter', ir, '--input', '-', '--raw-input', '44100', '1', '--output', dst], ok=False, data=raw)
        call(args, data=b'')
        repeatability_tests(d)
        speaker_map_tests(d)
        container_tests(d)
        other_writers_tests(d)
        failure_message_tests(d)
        long_path_tests(d)
        refused_sound_filter_tests(d)
        default_layout_tests(d)
        # CFG-3: a .cfg saved in Windows-1252 names ir_\xfc.wav for ir_u-umlaut.wav:
        # a failure that says so, not "IR file not found", with its code.
        (d / 'ansi.cfg').write_bytes(b'48000 1 1 0\n0\n0\nir_\xfc.wav\n0\n0\n0\n')
        err = call(['--filter', d / 'ansi.cfg', '--input', src, '--output', dst], ok=False).stderr.decode()
        assert "ansi.cfg:4:1: the file name 'ir_\\xFC.wav' is not UTF-8 text" in err, err
        assert 'Windows-1252' in err and 'Open ansi.cfg in a text editor and save it again as UTF-8' in err, err
        assert 'not found' not in err, err
        report = json.loads(call(['--validate', d / 'ansi.cfg', '--json'], ok=False).stdout)
        # The report's text holds the bytes too, so invalid-utf8 warns as well.
        assert report['diagnostics'][0]['code'] == 'not-utf8', report
        # An option value spelled like an inspection flag is still a value.
        pcm(d / 'v-ir.wav', [16384])
        pcm(d / 'v-in.wav', [1000, -1000])
        p = subprocess.run(
            [EXE, '--filter', 'v-ir.wav', '--input', 'v-in.wav', '--output', '--validate'],
            cwd=d,
            capture_output=True,
            check=False,
        )
        assert p.returncode == 0 and (d / '--validate').is_file(), p.stderr
    print('PASS: native CLI files, matrices, EOF, gain, errors and real RAW pipes')


if __name__ == '__main__':
    main()
