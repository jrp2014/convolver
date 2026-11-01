#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""convolver --compare: matches, the explained differences (shifted, short,
another level), refusals and exit codes. Standard library only."""

import struct
import subprocess
import sys
import tempfile
from pathlib import Path

EXE = str(Path(sys.argv[1]).resolve())
EXAMPLES = Path(__file__).resolve().parents[2] / 'examples/first-use'
STEREO = EXAMPLES / 'stereo'


def read(path):
    """Interleaved samples of a canonical 32-bit float WAV, with rate and channels."""
    data = path.read_bytes()
    channels, rate = struct.unpack_from('<HI', data, 22)
    size = struct.unpack_from('<I', data, 40)[0]
    return rate, channels, list(struct.unpack_from(f'<{size // 4}f', data, 44))


def write(path, rate, channels, samples):
    data = struct.pack(f'<{len(samples)}f', *samples)
    fmt = struct.pack('<HHIIHH', 3, channels, rate, rate * 4 * channels, 4 * channels, 32)
    path.write_bytes(
        b'RIFF' + struct.pack('<I', 4 + 8 + len(fmt) + 8 + len(data)) + b'WAVE'
        b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data
    )
    return path


def compare(output, expected, code):
    p = subprocess.run([EXE, '--compare', str(output), str(expected)], capture_output=True, check=False)
    # Windows writes text-mode newlines as CRLF.
    text = (p.stdout.decode('utf-8') + p.stderr.decode('utf-8')).replace('\r\n', '\n')
    assert p.returncode == code, (output, expected, p.returncode, text)
    return text


def main():
    expected = STEREO / 'expected.wav'
    rate, channels, samples = read(expected)
    with tempfile.TemporaryDirectory(prefix='convolver-compare-') as temporary:
        d = Path(temporary)
        # The CLI's own render matches, as float and as dithered 16-bit PCM.
        for name, form in (('float.wav', 'wav-float'), ('pcm16.wav', 'wav-pcm16')):
            subprocess.run(
                [
                    EXE,
                    '--filter',
                    str(STEREO / 'filter.cfg'),
                    '--input',
                    str(STEREO / 'input.wav'),
                    '--output',
                    str(d / name),
                    '--output-format',
                    form,
                ],
                check=True,
                capture_output=True,
            )
            text = compare(d / name, expected, 0)
            assert 'Match' in text and 'relative to the expected peak' in text, text
        # The crossover's quieter peak (0.32): dithered 16-bit still matches,
        # within two 16-bit steps rather than -80 dB of its peak.
        crossover = EXAMPLES / 'crossover'
        render = ['--filter', str(crossover / 'filter.cfg'), '--input', str(crossover / 'input.wav')]
        render += ['--output', str(d / 'crossover.wav'), '--output-format', 'wav-pcm16']
        subprocess.run([EXE, *render], check=True, capture_output=True)
        compare(d / 'crossover.wav', crossover / 'expected.wav', 0)
        assert 'Largest difference: 0\n' in compare(expected, expected, 0)
        # A render with silence after the tail (a host's longer tail) matches.
        longer = write(d / 'longer.wav', rate, channels, samples + [0.0] * channels * 1000)
        compare(longer, expected, 0)
        # Late by the plug-in's 256 frames: explained as uncompensated latency.
        late = write(d / 'late.wav', rate, channels, [0.0] * channels * 256 + samples)
        text = compare(late, expected, 1)
        assert 'started 256 frames (5.33 ms) earlier' in text and 'latency' in text, text
        # Early: the expected file's start is cut off, so only an expected file
        # that starts with silence can show it.
        padded = write(d / 'padded.wav', rate, channels, [0.0] * channels * 10 + samples)
        text = compare(expected, padded, 1)
        assert '10 frames (0.21 ms) later' in text and 'starts where the input does' in text, text
        # Cut short: explained as a missing tail.
        short = write(d / 'short.wav', rate, channels, samples[: channels * 4096])
        text = compare(short, expected, 1)
        assert 'ends 3048 frames (63.50 ms) too soon' in text and 'tail' in text, text
        # Late and short together: both are said.
        late_short = write(d / 'late_short.wav', rate, channels, [0.0] * channels * 256 + samples[: channels * 6000])
        text = compare(late_short, expected, 1)
        assert 'started 256 frames (5.33 ms) earlier' in text and 'also ends 1144 frames' in text, text
        # Empty and silent output.
        assert 'The output is empty' in compare(write(d / 'empty.wav', rate, channels, []), expected, 1)
        silent = write(d / 'silent.wav', rate, channels, [0.0] * len(samples))
        assert 'The output is silent' in compare(silent, expected, 1)
        # Inverted polarity.
        inverted = write(d / 'inverted.wav', rate, channels, [-s for s in samples])
        text = compare(inverted, expected, 1)
        assert 'polarity is reversed. ' in text, text
        # Another level: explained with the gain.
        quieter = write(d / 'quieter.wav', rate, channels, [s * 0.5 for s in samples])
        text = compare(quieter, expected, 1)
        assert '-6.02 dB relative to the expected file' in text, text
        # Swapped channels: nothing simpler explains it.
        swapped = [samples[i + 1 - 2 * (i % 2)] for i in range(len(samples))]
        text = compare(write(d / 'swapped.wav', rate, channels, swapped), expected, 1)
        assert 'Different' in text and 'Check that the output was rendered' in text, text
        # Another rate or channel count is refused before comparing.
        text = compare(write(d / 'rate.wav', 44100, channels, samples), expected, 1)
        assert 'at 44100 Hz and the expected file at 48000 Hz' in text, text
        text = compare(write(d / 'mono.wav', rate, 1, samples[::2]), expected, 1)
        assert 'has 1 channel and the expected file 2 channels' in text, text
        # Unreadable files and usage.
        compare(d / 'missing.wav', expected, 1)
        compare('-', expected, 2)
        for args in (['--compare'], ['--compare', str(expected)], ['--compare', 'a', 'b', 'c']):
            p = subprocess.run([EXE, *args], capture_output=True, check=False)
            assert p.returncode == 2 and b'Usage: convolver --compare' in p.stderr, (args, p)
        p = subprocess.run([EXE, '--compare', '--help'], capture_output=True, check=True)
        assert b'Exit status: 0 the files match' in p.stdout
        assert b'--compare OUTPUT EXPECTED' in subprocess.run([EXE, '--help'], capture_output=True, check=True).stdout
    print('compare: matches, explained differences and refusals passed')


if __name__ == '__main__':
    main()
