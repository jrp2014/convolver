#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""--inspect / --validate command-line contracts: modes, exit codes, JSON schema 1,
escaping and determinism. Standard library only."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

EXE = str(Path(sys.argv[1]).resolve())


def wav(path, values, rate=48000, channels=1):
    data = struct.pack('<%df' % len(values), *values)
    path.write_bytes(struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', 36 + len(data), b'WAVE', b'fmt ',
                                 16, 3, channels, rate, rate * channels * 4, channels * 4, 32,
                                 b'data', len(data)) + data)


def call(args, code, cwd=None):
    p = subprocess.run([EXE, *args], capture_output=True, cwd=cwd)
    assert p.returncode == code, (args, p.returncode, p.stdout, p.stderr)
    return p


def report(args, code=0, cwd=None):
    p = call([*args, '--json'], code, cwd)
    text = p.stdout.decode('utf-8')
    assert text.count('\n') == 1 and text.endswith('\n'), 'one JSON document per line'
    data = json.loads(text)
    assert data['schema_version'] == 1
    assert data['valid'] == (code == 0), data
    return data


def damaged_audio_reports():
    # A failed direct sound-file leaf must not abort JSON or its valid sibling.
    with tempfile.TemporaryDirectory(prefix='convolver-damaged-') as t:
        d = Path(t)
        wav(d / 'bad.wav', [float('nan')])
        wav(d / 'good.wav', [1])
        (d / 'inner.txt').write_text('bad.wav\ngood.wav\n')
        (d / 'outer.txt').write_text('inner.txt\n')
        r = report(['--inspect', 'bad.wav'], 1, d)
        assert r['diagnostics'][0]['code'] == 'damaged-asset'
        assert r['diagnostics'][0]['file'] == 'bad.wav'
        r = report(['--validate', 'outer.txt', '--all-configurations'], 1, d)
        assert [c['valid'] for c in r['configurations']] == [False, True]
        diag = r['diagnostics'][0]
        assert diag['code'] == 'damaged-asset' and diag['file'] == 'bad.wav'
        assert diag['include_chain'] == [
            {'file': 'outer.txt', 'line': 1, 'column': 1},
            {'file': 'inner.txt', 'line': 1, 'column': 1}], diag


def filesystem_error_reports():
    # POSIX symlink loops exercise real canonicalization failures. Windows
    # symlink creation needs privileges; the resolver unit test covers it there.
    if os.name == 'nt':
        return
    with tempfile.TemporaryDirectory(prefix='convolver-symlink-') as t:
        d = Path(t)
        (d / 'loop.cfg').symlink_to('loop.cfg')
        wav(d / 'good.wav', [1])
        (d / 'inner.txt').write_text('loop.cfg\ngood.wav\n')
        (d / 'outer.txt').write_text('inner.txt\n')
        r = report(['--inspect', 'loop.cfg'], 1, d)
        assert r['diagnostics'][0]['code'] == 'unreadable'
        r = report(['--validate', 'outer.txt', '--all-configurations'], 1, d)
        assert [c['file'] for c in r['configurations']] == ['good.wav']
        assert r['configurations'][0]['valid']
        diag = r['diagnostics'][0]
        assert diag['file'] == 'loop.cfg'
        assert diag['include_chain'] == [
            {'file': 'outer.txt', 'line': 1, 'column': 1},
            {'file': 'inner.txt', 'line': 1, 'column': 1}], diag
        # A referenced IR needs its configuration location, too.
        (d / 'ir.cfg').write_text('48000 1 1 0\n0\n0\nloop.cfg\n0\n0\n0\n')
        r = report(['--inspect', 'ir.cfg'], 1, d)
        diag = r['diagnostics'][0]
        assert diag['code'] == 'unreadable' and diag['file'] == 'ir.cfg'
        assert diag['line'] == 4 and diag['column'] == 1, diag


def invalid_utf8_warnings():
    if os.name == 'nt':
        return  # Windows paths cannot contain arbitrary non-UTF-8 bytes.
    with tempfile.TemporaryDirectory(prefix='convolver-encoding-') as t:
        d = Path(t)
        (d / 'bytes.cfg').write_bytes(b'48000 1 1 0\n0\n0\nbad\xff.wav\n0\n0\n0\n')
        args = ['--inspect', 'bytes.cfg', '--syntax-only']
        r = report(args, cwd=d)
        assert [x['code'] for x in r['diagnostics']] == ['invalid-utf8']
        r = report([*args, '--warnings-as-errors'], 1, d)
        assert [x['code'] for x in r['diagnostics']] == ['invalid-utf8']
        p = call([*args, '--warnings-as-errors'], 1, d)
        assert b'[invalid-utf8]' in p.stderr and b'Invalid' in p.stdout
        # Invalid bytes only in an error message must also be noticed.
        (d / 'error.cfg').write_bytes(b'48000 1 1 0\n0\n0\nir.wav\n0\nwrong\xff\n0\n')
        r = report(['--inspect', 'error.cfg', '--syntax-only'], 1, d)
        assert any(x['code'] == 'invalid-utf8' for x in r['diagnostics']), r


def raw_filter_reports():
    # A headerless .pcm filter: its assumed sample type is shown, and can be chosen.
    with tempfile.TemporaryDirectory(prefix='convolver-rawfilter-') as t:
        d = Path(t)
        (d / 'drc.pcm').write_bytes(struct.pack('<2f', .5, .25))
        (d / 'raw.cfg').write_text('48000 1 1 0\n0\n0\ndrc.pcm\n0\n0\n0\n')
        wav(d / 'ir.wav', [1])
        (d / 'wav.cfg').write_text('48000 1 1 0\n0\n0\nir.wav\n0\n0\n0\n')
        path = report(['--inspect', 'raw.cfg'], 0, d)['configurations'][0]['paths'][0]['ir']
        assert path['raw_format'] == 'f32le' and path['frames'] == 2, path
        assert b'raw mono f32le' in call(['--inspect', 'raw.cfg'], 0, d).stdout
        path = report(['--inspect', 'raw.cfg', '--raw-filter-format', 's16be'], 0,
                      d)['configurations'][0]['paths'][0]['ir']
        assert path['raw_format'] == 's16be' and path['frames'] == 4, path
        assert report(['--inspect', 'wav.cfg'], 0, d)['configurations'][0]['paths'][0][
            'ir']['raw_format'] is None
        call(['--inspect', 'raw.cfg', '--raw-filter-format', 'u8'], 2, d)


def main():
    raw_filter_reports()
    assert b'--inspect' in call(['--help'], 0).stdout
    assert b'--syntax-only' in call(['--inspect', '--help'], 0).stdout
    with tempfile.TemporaryDirectory(prefix='convolver-inspect-') as t:
        d = Path(t)
        wav(d / 'ir.wav', [1, -.5, .25])
        wav(d / 'ir44.wav', [1], rate=44100)
        (d / 'room.cfg').write_text('48000 2 2 0\n0 0.5\n0 0\nir.wav\n0\n-0 1.25\n1\n')
        (d / 'absent.cfg').write_text('// header\n48000 1 1 0\n0\n0\nmissing.wav\n0\n0\n0\n')
        (d / 'list.txt').write_text('ir44.wav\nabsent.cfg\nroom.cfg\n')
        before = sorted(d.rglob('*'))

        r = report(['--inspect', str(d / 'room.cfg')])
        leaf = r['configurations'][0]
        assert r['mode'] == 'inspect' and r['validation'] == 'selected'
        assert r['convolver']['fft_backend'] and r['convolver']['sndfile_version'] and r['convolver']['cpu_kernels'] in ('AVX2+FMA', 'baseline')
        assert leaf['paths'][0]['inputs'] == [{'token': '-0', 'channel': 0, 'gain': -1},
                                              {'token': '1.25', 'channel': 1, 'gain': 0.25}]
        assert leaf['input_delays'][1] == {'token': '0.5', 'milliseconds': 0.5, 'frames': 24}
        assert leaf['timing'] == {'latency_frames': 256, 'tail_frames': 26}
        assert leaf['resources']['accounted_buffer_bytes'] > 0
        assert 'FFT plan internals' in leaf['resources']['excludes']
        assert report(['--inspect', str(d / 'room.cfg'), '--block-size', '3'])[
            'configurations'][0]['timing']['latency_frames'] == 3

        # Determinism: the same inputs give byte-identical reports.
        first = call(['--inspect', 'list.txt', '--json', '--sample-rate', '48000'], 1, d).stdout
        assert first == call(['--inspect', 'list.txt', '--json', '--sample-rate', '48000'], 1,
                             d).stdout

        # Selection: the missing IR of an entry before the match fails selected mode
        # only when that entry is selected.
        r = report(['--inspect', 'list.txt', '--sample-rate', '48000', '--input-channels', '2'],
                   cwd=d)
        assert r['selection']['selected'] == 'room.cfg'
        assert [s['outcome'] for s in r['selection']['traversal']] == [
            'list', 'rate-and-input-mismatch', 'input-mismatch', 'selected']
        assert r['selection']['expected_output_channels'] is None
        r = report(['--inspect', 'list.txt', '--output-channels', '2'], cwd=d)
        assert r['selection']['selected'] == 'room.cfg'
        assert r['selection']['expected_output_channels'] == 2
        assert [(s['outcome'], s['output_channels']) for s in r['selection']['traversal']] == [
            ('list', None), ('output-mismatch', 1), ('output-mismatch', 1), ('selected', 2)]
        call(['--inspect', 'list.txt', '--all-configurations', '--output-channels', '2'], 2, d)
        r = report(['--validate', 'list.txt', '--sample-rate', '48000'], 1, d)
        diag = r['diagnostics'][0]
        assert diag['code'] == 'missing-asset' and diag['file'] == 'absent.cfg' and \
            diag['line'] == 5 and diag['column'] == 1, diag
        assert diag['include_chain'] == [{'file': 'list.txt', 'line': 2, 'column': 1}], diag
        assert all(c['timing']['tail_frames'] is None for c in r['configurations'])
        r = report(['--validate', 'list.txt', '--all-configurations'], 1, d)
        assert [c['valid'] for c in r['configurations']] == [True, False, True]

        # Syntax-only never opens IR files and reports unverified values as null.
        r = report(['--inspect', str(d / 'absent.cfg'), '--syntax-only'])
        leaf = r['configurations'][0]
        assert r['validation'] == 'syntax-only' and not leaf['coefficients_verified']
        assert leaf['timing'] == {'latency_frames': None, 'tail_frames': None}
        assert leaf['resources']['accounted_buffer_bytes'] is None
        assert leaf['paths'][0]['ir']['frames'] is None and leaf['paths'][0]['partitions'] is None
        r = report(['--inspect', str(d / 'list.txt'), '--syntax-only'], 1)
        assert r['diagnostics'][0]['code'] == 'syntax-only-scope'

        # Validate prints a one-line summary; diagnostics go to stderr.
        p = call(['--validate', str(d / 'room.cfg')], 0)
        assert p.stdout.startswith(b'valid: ') and not p.stderr
        p = call(['--validate', str(d / 'absent.cfg')], 1)
        assert b'absent.cfg:5:1:' in p.stderr and b'[missing-asset]' in p.stderr
        assert p.stdout.startswith(b'invalid: ')
        p = call(['--inspect', str(d / 'room.cfg')], 0)
        assert b'latency 256 frames, tail 26 frames' in p.stdout

        # Profiles add warnings; only --warnings-as-errors makes them fail.
        (d / 'abs.cfg').write_text('48000 1 1 0\n0\n0\n%s\n0\n0\n0\n' %
                                   (d / 'ir.wav').resolve().as_posix())
        p = call(['--validate', str(d / 'abs.cfg'), '--profile', 'portable-paths'], 0)
        assert b'[absolute-reference]' in p.stderr and p.stdout.startswith(b'valid: ')
        assert not call(['--validate', str(d / 'abs.cfg')], 0).stderr
        r = report(['--validate', str(d / 'abs.cfg'), '--profile', 'portable-paths',
                    '--warnings-as-errors'], 1)
        assert r['warnings_as_errors'] and r['profile'] == 'portable-paths'
        assert [x['severity'] for x in r['diagnostics']] == ['warning']
        call(['--validate', str(d / 'abs.cfg'), '--warnings-as-errors'], 0)
        (d / 'abs.cfg').unlink()

        # Usage errors exit 2 without a report.
        cfg = str(d / 'room.cfg')
        for args in (['--inspect', cfg, '--validate', cfg], ['--inspect'],
                     ['--inspect', cfg, '--output', str(d / 'x.wav')],
                     ['--validate', cfg, '--input', cfg], ['--inspect', cfg, '--keep-latency'],
                     ['--inspect', cfg, '--all-configurations', '--sample-rate', '48000'],
                     ['--inspect', cfg, '--all-configurations', '--syntax-only'],
                     ['--inspect', cfg, '--profile', 'roon'], ['--inspect', cfg, '--block-size', '0'],
                     ['--inspect', '-'], ['--inspect', cfg, '--fft-wisdom', cfg]):
            p = call(args, 2)
            assert not p.stdout.startswith(b'{'), args
        # Unreadable configuration: a report with a diagnostic, exit 1.
        r = report(['--inspect', str(d / 'none.cfg')], 1)
        assert r['diagnostics'][0]['code'] == 'unreadable'

        # Names needing JSON escapes stay valid JSON and round-trip.
        odd = 'we"ird\\ ü' if os.name != 'nt' else "we'ird ü"
        if os.name != 'nt':
            odd += '\t'
        (d / (odd + '.cfg')).write_text('48000 1 1 0\n0\n0\nir.wav\n0\n0\n0\n')
        r = report(['--inspect', odd + '.cfg'], cwd=d)
        assert r['selection']['selected'] == odd + '.cfg', r['selection']
        if sys.platform.startswith('linux'):
            # Linux filenames are bytes; invalid UTF-8 must not produce malformed JSON.
            bad = os.fsencode(d) + b'/bad\xff.cfg'
            with open(bad, 'w') as f:
                f.write('48000 1 1 0\n0\n0\nir.wav\n0\n0\n0\n')
            p = subprocess.run([EXE, '--inspect', bad, '--json'], capture_output=True)
            data = json.loads(p.stdout.decode('utf-8'))
            assert '�' in data['selection']['path']
            assert any(x['code'] == 'invalid-utf8' for x in data['diagnostics'])
            os.unlink(bad)
            (d / (odd + '.cfg')).unlink()
        else:
            (d / (odd + '.cfg')).unlink()

        # No output, wisdom or other files were created.
        assert sorted(d.rglob('*')) == before, set(d.rglob('*')) ^ set(before)
    # Bundled pre-portable samples are diagnosed (exit 1), never crash or pass.
    samples = Path(__file__).resolve().parents[2] / 'historical' / 'configs'
    for sample in sorted(samples.rglob('*.txt')):
        p = call(['--validate', str(sample), '--all-configurations', '--json'], 1)
        assert json.loads(p.stdout)['diagnostics'], sample
    print('PASS: inspection CLI modes, exit codes, JSON escaping and determinism')


if __name__ == '__main__':
    damaged_audio_reports()
    filesystem_error_reports()
    invalid_utf8_warnings()
    main()
