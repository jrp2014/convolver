#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Render the compatibility corpus with the Convolver CLI and check every result
against an independent double-precision direct-convolution reference.

Expected audio is computed only from the manifest's stated graph (routes, delays,
IR samples). The CLI is never used to produce expectations.

Usage: run_corpus.py CONVOLVER_EXE [CASE_ID ...]
"""
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_fixtures as fixtures  # noqa: E402

PLATFORM = 'windows' if os.name == 'nt' else 'posix'


def filesystem_case(directory):
    probe = Path(directory) / 'Case-Probe'
    probe.write_bytes(b'')
    try:
        return 'case-insensitive' if (Path(directory) / 'case-probe').exists() else 'case-sensitive'
    finally:
        probe.unlink()


def wave_channel_mask(path):
    """dwChannelMask of a WAVE_FORMAT_EXTENSIBLE header, or None for a plain header."""
    data = Path(path).read_bytes()
    pos = 12
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            fmt = struct.unpack_from('<H', data, pos + 8)[0]
            return struct.unpack_from('<I', data, pos + 28)[0] if fmt == 0xfffe else None
        pos += 8 + size + (size & 1)
    raise AssertionError('missing fmt chunk')


def read_float_wav(path):
    data = Path(path).read_bytes()
    assert data[:4] in (b'RIFF', b'RF64') and data[8:12] == b'WAVE', 'not a WAV file'
    pos, channels, payload = 12, None, None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            fmt, channels = struct.unpack_from('<HH', data, pos + 8)
            assert fmt in (3, 65534), 'expected float WAV output, format %d' % fmt
        elif tag == b'data':
            payload = data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    assert channels and payload is not None, 'missing fmt or data chunk'
    return channels, struct.unpack('<%df' % (len(payload) // 4), payload)


def ir_samples(case, name, channel):
    spec = case['files'][name]
    values = spec['channels'][channel]
    frames = max(len(c) for c in spec['channels'])
    values = list(values) + [0] * (frames - len(values))
    if spec['format'] == 'pcm16':
        return [v / 32768.0 for v in values]
    return [float(v) for v in values]


def reference(case, inputs):
    """Double-precision direct convolution of the manifest's stated graph."""
    exp = case['expect']
    frames_in = len(inputs[0]) if inputs else 0
    in_delay, out_delay = exp['input_delays'], exp['output_delays']
    paths = []
    for p in exp['paths']:
        h = ir_samples(case, p['ir'], p['channel'])
        mix_len = frames_in + max(in_delay[c] for c, _ in p['inputs'])
        mix = [0.0] * mix_len
        for c, g in p['inputs']:
            for t, v in enumerate(inputs[c]):
                mix[t + in_delay[c]] += g * v
        paths.append((mix, h, p['outputs']))
    length = 1 + max(len(m) + len(h) - 2 + max(out_delay[o] for o, _ in outs)
                     for m, h, outs in paths)
    out = [[0.0] * length for _ in range(exp['output_channels'])]
    for mix, h, outs in paths:
        y = [0.0] * (len(mix) + len(h) - 1)
        for i, x in enumerate(mix):
            if x:
                for j, w in enumerate(h):
                    y[i + j] += x * w
        for o, g in outs:
            for t, v in enumerate(y):
                out[o][t + out_delay[o]] += g * v
    return out


def compare(case_id, actual, expected, frames, offset):
    channels = len(expected)
    assert len(actual) == (frames + offset) * channels, (
        '%s: %d output frames, expected %d' % (case_id, len(actual) // channels, frames + offset))
    for ch, values in enumerate(expected):
        for t, v in enumerate(values[frames:]):
            assert abs(v) < 1e-12, '%s: reference signal beyond declared frame count at %d' % (
                case_id, frames + t)
    for t in range(frames + offset):
        for ch in range(channels):
            a = actual[t * channels + ch]
            e = expected[ch][t - offset] if t >= offset and t - offset < len(expected[ch]) else 0.0
            bound = 2e-5 * (1 + max(abs(a), abs(e)))
            assert math.isfinite(a) and abs(a - e) <= bound, (
                '%s: frame %d channel %d got %r expected %r' % (case_id, t, ch, a, e))


def run(exe, args, cwd, data=None, chunk=None):
    if chunk is None:
        return subprocess.run([exe, *args], cwd=cwd, input=data, capture_output=True, timeout=120)
    with subprocess.Popen([exe, *args], cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE) as p:
        # stdout is drained after all writes; outputs here are far below pipe capacity
        # only for short inputs, so keep the corpus's RAW cases small.
        for i in range(0, len(data), chunk):
            p.stdin.write(data[i:i + chunk])
            p.stdin.flush()
        p.stdin.close()
        p.stdin = None
        out, err = p.communicate(timeout=120)
        return subprocess.CompletedProcess(p.args, p.returncode, out, err)


def portable(exe, case, filter_arg, cwd, spec):
    """The portable-paths profile reports exactly the expected warnings."""
    args = ['--validate', filter_arg, '--json', '--profile', 'portable-paths', '--sample-rate',
            str(spec['rate']), '--input-channels', str(spec['channels'])]
    p = run(exe, args, cwd)
    report = json.loads(p.stdout.decode('utf-8'))
    warnings = sorted(d['code'] for d in report['diagnostics'] if d['severity'] == 'warning')
    assert warnings == sorted(case['portable_paths_warnings']), (case['id'], warnings)
    if 'error' not in case['expect']:
        strict = run(exe, args + ['--warnings-as-errors'], cwd)
        assert strict.returncode == (1 if warnings else 0), (case['id'], strict.returncode)
        native = json.loads(run(exe, args[:4] + ['convolver'] + args[5:], cwd).stdout)
        assert not native['diagnostics'], (case['id'], 'native profile warned')


def inspect(exe, case, root, filter_arg, cwd, manifest, block=17):
    """The inspection report must describe the same graph the render produced."""
    spec = case['input']
    p = run(exe, ['--inspect', filter_arg, '--json', '--sample-rate', str(spec['rate']),
                  '--input-channels', str(spec['channels']), '--block-size', str(block)], cwd)
    report = json.loads(p.stdout.decode('utf-8'))
    cid, expect = case['id'], case['expect']
    assert report['schema_version'] == 1, cid
    if 'portable_paths_warnings' in case:
        portable(exe, case, filter_arg, cwd, spec)
    if 'error' in expect:
        codes = manifest['error_codes'][expect['error']]
        assert p.returncode == 1 and not report['valid'], '%s: inspection accepted it' % cid
        found = [d['code'] for d in report['diagnostics'] if d['severity'] == 'error']
        assert found and found[0] in codes, '%s: diagnostic codes %r, expected one of %r' % (
            cid, found, codes)
        return
    assert p.returncode == 0 and report['valid'], '%s: %s' % (cid, report['diagnostics'])
    selected = report['selection']['selected']
    assert (cwd / selected).resolve() == (root / expect['selected']).resolve(), (cid, selected)
    leaf = report['configurations'][0]
    assert leaf['coefficients_verified'], cid
    assert leaf['output_channels'] == expect['output_channels'], cid
    assert leaf['channel_mask'] == expect.get('channel_mask', 0), cid
    assert [d['frames'] for d in leaf['input_delays']] == expect['input_delays'], cid
    assert [d['frames'] for d in leaf['output_delays']] == expect['output_delays'], cid
    assert len(leaf['paths']) == len(expect['paths']), cid
    for got, want in zip(leaf['paths'], expect['paths']):
        assert got['ir']['channel'] == want['channel'], cid
        for side in ('inputs', 'outputs'):
            routes = [[r['channel'], r['gain']] for r in got[side]]
            assert len(routes) == len(want[side]) and all(
                a[0] == b[0] and abs(a[1] - b[1]) < 1e-6 for a, b in zip(routes, want[side])), (
                cid, side, routes)
    timing = leaf['timing']
    assert timing['latency_frames'] == block, cid
    assert timing['tail_frames'] == expect['frames'] - spec['frames'], (
        '%s: inspected tail %d, rendered tail %d' % (cid, timing['tail_frames'],
                                                     expect['frames'] - spec['frames']))


def check_case(exe, manifest, case, root):
    if PLATFORM not in case.get('only', [PLATFORM]):
        return 'skipped (%s)' % PLATFORM
    expect = case.get('platform_expect', {}).get(PLATFORM, case['expect'])
    expect = case.get('filesystem_expect', {}).get(filesystem_case(root.parent), expect)
    case = dict(case, expect=expect)
    inputs = fixtures.generate_case(case, root)
    cwd = root / case.get('cwd', 'elsewhere')
    if case.get('filter_arg') == 'relative':
        filter_arg = os.path.relpath(root / case['filter'], cwd)
    else:
        filter_arg = str((root / case['filter']).resolve())
    output = root / 'elsewhere' / 'output.wav'
    if 'error' in expect:
        if output.exists():
            output.unlink()
        p = run(exe, ['--filter', filter_arg, '--input', str(root / 'input.wav'),
                      '--output', str(output)], cwd)
        stderr = p.stderr.decode('utf-8', 'replace')
        pattern = manifest['error_patterns'][expect['error']]
        assert p.returncode == 1, '%s: exit %d, expected 1\n%s' % (case['id'], p.returncode, stderr)
        assert re.search(pattern, stderr), '%s: diagnostic %r does not match %r' % (
            case['id'], stderr, pattern)
        assert not output.exists(), '%s: failed render created output' % case['id']
        inspect(exe, case, root, filter_arg, cwd, manifest)
        return 'diagnosed ' + expect['error']
    expected = reference(case, inputs)
    defaults = manifest['defaults']
    runs = 0
    for block in case.get('block_sizes', defaults['block_sizes']):
        for mode in case.get('latency_modes', defaults['latency_modes']):
            extra = ['--block-size', str(block)] + (['--keep-latency'] if mode == 'keep' else [])
            offset = block if mode == 'keep' else 0
            if 'stdin_chunk_bytes' in case:
                spec = case['input']
                frames = len(inputs[0])
                raw = struct.pack('<%df' % (frames * spec['channels']),
                                  *[inputs[c][f] for f in range(frames)
                                    for c in range(spec['channels'])])
                p = run(exe, ['--filter', filter_arg, '--input', '-', '--raw-input',
                              str(spec['rate']), str(spec['channels']), '--output', '-',
                              '--output-format', 'raw-float', *extra], cwd, raw,
                        case['stdin_chunk_bytes'])
                assert p.returncode == 0, '%s: %s' % (case['id'], p.stderr.decode('utf-8', 'replace'))
                actual = struct.unpack('<%df' % (len(p.stdout) // 4), p.stdout)
            else:
                p = run(exe, ['--filter', filter_arg, '--input', str(root / 'input.wav'),
                              '--output', str(output), *extra], cwd)
                assert p.returncode == 0, '%s block %d: %s' % (
                    case['id'], block, p.stderr.decode('utf-8', 'replace'))
                channels, actual = read_float_wav(output)
                assert channels == expect['output_channels'], (
                    '%s: %d output channels' % (case['id'], channels))
            compare('%s block %d %s' % (case['id'], block, mode), actual, expected,
                    expect['frames'], offset)
            runs += 1
    if case.get('speaker_map'):
        # Opt-in labels: the header carries the configuration's mask, samples unchanged.
        p = run(exe, ['--filter', filter_arg, '--input', str(root / 'input.wav'),
                      '--output', str(output), '--speaker-map', 'config'], cwd)
        assert p.returncode == 0, '%s speaker map: %s' % (case['id'], p.stderr.decode())
        mask = wave_channel_mask(output)
        assert mask == expect['channel_mask'], '%s: speaker mask %r' % (case['id'], mask)
        compare('%s speaker map' % case['id'], read_float_wav(output)[1], expected,
                expect['frames'], 0)
        runs += 1
    elif output.exists():
        assert wave_channel_mask(output) is None, '%s: unrequested speaker mask' % case['id']
    inspect(exe, case, root, filter_arg, cwd, manifest)
    return 'rendered %d ways, inspected' % runs


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    exe = str(Path(argv[1]).resolve())
    manifest = fixtures.load_manifest()
    wanted = set(argv[2:])
    ids = [c['id'] for c in manifest['cases']]
    assert len(ids) == len(set(ids)), 'duplicate case ids'
    labels = {'spec-derived', 'current-behavior', 'consumer-verified'}
    failures = 0
    with tempfile.TemporaryDirectory(prefix='convolver-corpus-') as temp:
        for case in manifest['cases']:
            if wanted and case['id'] not in wanted:
                continue
            assert case['label'] in labels and case.get('evidence'), case['id']
            if case['label'] == 'consumer-verified':
                assert all(case.get(k) for k in ('consumer', 'revision', 'date', 'evidence_type'))
            try:
                print('%-34s %s' % (case['id'], check_case(exe, manifest, case,
                                                           Path(temp) / case['id'])))
            except AssertionError as e:
                failures += 1
                print('FAIL %s' % e)
    if failures:
        print('%d corpus case(s) failed' % failures)
        return 1
    print('PASS: configuration compatibility corpus')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
