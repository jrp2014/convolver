#!/usr/bin/env python3
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


def main():
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
        assert r['convolver']['fft_backend'] and r['convolver']['sndfile_version']
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
    print('PASS: inspection CLI modes, exit codes, JSON escaping and determinism')


if __name__ == '__main__':
    main()
