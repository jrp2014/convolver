#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""End-to-end CLI checks through real FFTW and libsndfile, no Python packages."""

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
            [str(nan_input), 'at 0.000 s (frame 1), channel 2', 'export it again'],
            True,
        ),
        (
            ['--filter', ir, '--input', loud, '--output', out, '--gain-db', '12'],
            None,
            ['output overflowed at 0.000 s (frame 0), channel 1', 'lower --gain-db', 'no-clip'],
            True,
        ),
        (
            ['--filter', ir, *raw_args],
            struct.pack('<2f', 0.5, float('inf')),
            ['from standard input', 'frame 1', 'read as f32le'],
            True,
        ),
        (['--filter', ir, '--input', garbage, '--output', out], None, [str(garbage), 'export it again'], False),
        (['--filter', ir, '--input', bad_fmt, '--output', out], None, [str(bad_fmt), 'export it again'], False),
        (
            ['--filter', nan_ir, '--input', good, '--output', out],
            None,
            ['cannot read audio file', 'frame 1), channel 1', 'export it again'],
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
            [str(truncated), 'RIFF/WAVE', 'export it again'],
            False,
        ),
        (
            ['--filter', damaged_cfg, '--input', good, '--output', out],
            None,
            ['msg-truncated.wav', 'export it again'],
            False,
        ),
        (
            ['--filter', binary, '--input', good, '--output', out],
            None,
            ['neither a configuration', 'export it again'],
            False,
        ),
        (['--filter', ir, *raw_args], b'\0\0\0', ['incomplete frame', 'channel count and sample type'], True),
    )
    for args, data, texts, incomplete in cases:
        p = call(args, ok=False, data=data)
        message = p.stderr.decode()
        for text in texts:
            assert text in message, (args, text, message)
        shown = 'standard output' if args[args.index('--output') + 1] == '-' else str(out)
        assert ('is incomplete' in message and shown in message) == incomplete, (args, message)
    # The CLI replaces an existing output, as command-line tools usually do.
    out.write_bytes(b'not audio')
    call(['--filter', ir, '--input', good, '--output', out])
    assert floats(out)[0] == 1


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
        cfg = d / 'sm.cfg'
        cfg.write_text(
            f'48000 2 {outputs} {mask:x}\n0 0\n{" ".join("0" * outputs)}\n'
            + ''.join(f'sm-ir.wav\n0\n0\n{o}.{o + 1}\n' for o in range(outputs))
        )
        return cfg

    for mask, outputs, fmt in ((0x3, 2, 'wav-float'), (0x3F, 6, 'wav-pcm24'), (0x3F, 6, 'wav-float')):
        cfg = config(mask, outputs)
        base = ['--filter', cfg, '--input', src, '--output', out, '--output-format', fmt]
        call(base)
        plain = out.read_bytes()
        assert channel_mask(out) is None
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
    for args in ([config(0x7, 2)], [config(0x3, 2), '--output-format', 'raw-float'], [config(0x40001, 2)]):
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
        output_selection_tests(d)
        # Errors must be failures, not successful silent output.
        for extra in (['--block-size', '0'], ['--block-size', '-1'], ['--gain-db', 'nan'], ['--unknown']):
            call(['--filter', ir, '--input', src, '--output', dst, *extra], ok=False)
        call(['--filter', ir, '--input', src, '--output', src], ok=False)
        call(['--filter', ir, '--input', '-', '--output', dst], ok=False, data=raw)
        call(['--filter', ir, '--input', '-', '--raw-input', '44100', '1', '--output', dst], ok=False, data=raw)
        call(args, data=b'')
        repeatability_tests(d)
        speaker_map_tests(d)
        container_tests(d)
        failure_message_tests(d)
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
