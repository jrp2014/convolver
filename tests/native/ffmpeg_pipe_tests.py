#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The FFmpeg pipe of docs/use/command-line.md#with-ffmpeg, run as written
(FFMPEG-1): FFmpeg decodes one audio track of a film, Convolver filters it
through RAW streams and FFmpeg remuxes it with the film's video.

Checks, with the first-use 2-in/6-out crossover: `-map 0:a:1` selects the
second audio track; the 6-channel result keeps every frame, the filter's
tail included, and matches the example's expected output after FLAC's
24 bits; the video is copied; and audio that starts late in the film starts
at time zero afterwards, as the guide warns.

Usage: ffmpeg_pipe_tests.py CONVOLVER FFMPEG FFPROBE. Standard library only.
"""

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

CONVOLVER, FFMPEG, FFPROBE = (str(Path(argument).resolve()) for argument in sys.argv[1:4])
CROSSOVER = Path(__file__).resolve().parents[2] / 'examples/first-use/crossover'
# FLAC keeps 24 bits (a step is 1.2e-7; FFmpeg's conversion may truncate),
# and the engine's float result differs from the double-precision expected
# output by about 1e-7. 16 bits would miss by 1.5e-5.
TOLERANCE = 1e-6


def run(command, **options):
    p = subprocess.run(command, capture_output=True, check=False, **options)
    assert p.returncode == 0, (command, p.returncode, p.stderr.decode('utf-8', 'replace'))
    return p.stdout


def wav_samples(path):
    """Interleaved samples and channel count of a canonical 32-bit float WAV."""
    data = path.read_bytes()
    channels = struct.unpack_from('<H', data, 22)[0]
    size = struct.unpack_from('<I', data, 40)[0]
    return channels, struct.unpack_from(f'<{size // 4}f', data, 44)


def film(path, audio_offset=None):
    """A short film: test-pattern video, then a 440 Hz tone, then the
    crossover's input, as float PCM tracks 0:a:0 and 0:a:1."""
    offset = ['-itsoffset', str(audio_offset)] if audio_offset else []
    run(
        [
            FFMPEG,
            '-v',
            'error',
            '-y',
            '-f',
            'lavfi',
            '-i',
            'testsrc=size=64x48:rate=25:duration=0.2',
            '-f',
            'lavfi',
            '-i',
            'sine=frequency=440:sample_rate=48000:duration=0.2',
            *offset,
            '-i',
            str(CROSSOVER / 'input.wav'),
            '-map',
            '0:v',
            '-map',
            '1:a',
            '-map',
            '2:a',
            '-c:v',
            'ffv1',
            '-c:a',
            'pcm_f32le',
            '-ac:a:0',
            '2',
            str(path),
        ]
    )
    return path


def corrected(movie, output, track='0:a:1'):
    """The guide's pipe, with its 2-in/6-out change (`-ac 6` when receiving)."""
    decode = [FFMPEG, '-v', 'error', '-i', str(movie), '-map', track, '-f', 'f32le', '-ar', '48000']
    decode += ['-ac', '2', '-']
    convolve = [CONVOLVER, '--filter', str(CROSSOVER / 'filter.cfg'), '--input', '-']
    convolve += ['--raw-input', '48000', '2', '--output', '-', '--output-format', 'raw-float']
    remux = [FFMPEG, '-v', 'error', '-y', '-i', str(movie), '-f', 'f32le', '-ar', '48000', '-ac', '6']
    remux += ['-i', '-', '-map', '0:v:0', '-map', '1:a:0', '-c:v', 'copy', '-c:a', 'flac', str(output)]
    first = subprocess.Popen(decode, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    second = subprocess.Popen(convolve, stdin=first.stdout, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    first.stdout.close()
    third = subprocess.run(remux, stdin=second.stdout, capture_output=True, check=False)
    second.stdout.close()
    second_error = second.stderr.read()
    first_error = first.stderr.read()
    for name, code, error in (
        ('ffmpeg (decode)', first.wait(), first_error),
        ('convolver', second.wait(), second_error),
        ('ffmpeg (remux)', third.returncode, third.stderr),
    ):
        assert code == 0, (name, code, error.decode('utf-8', 'replace'))
    return output


def streams(path):
    text = run([FFPROBE, '-v', 'error', '-show_streams', '-of', 'json', str(path)])
    return json.loads(text)['streams']


def video(path):
    """Checksums of each video packet, to show the video was copied."""
    return run([FFMPEG, '-v', 'error', '-i', str(path), '-map', '0:v', '-c', 'copy', '-f', 'framemd5', '-'])


def audio(path):
    """The first audio track of a file, decoded to interleaved floats."""
    data = run([FFMPEG, '-v', 'error', '-i', str(path), '-map', '0:a:0', '-f', 'f32le', '-'])
    return struct.unpack(f'<{len(data) // 4}f', data)


def piped_wav(d, frames):
    """IO-10: a WAV that FFmpeg wrote to a pipe, where it cannot go back to
    fill in the sizes, saved as a file. An FFmpeg run stopped half way leaves
    the same header, so Convolver refuses the file and says why and what to
    do; the same audio written by FFmpeg to a file renders whole."""
    source = CROSSOVER / 'input.wav'
    piped = d / 'piped.wav'
    encode = [FFMPEG, '-v', 'error', '-y', '-i', str(source), '-c:a', 'pcm_f32le', '-f', 'wav']
    piped.write_bytes(run([*encode, '-']))
    header = piped.read_bytes()
    riff_size = struct.unpack_from('<I', header, 4)[0]
    data_size = struct.unpack_from('<I', header, header.index(b'data') + 4)[0]
    assert riff_size == data_size == 0xFFFFFFFF, (
        'FFmpeg no longer leaves the sizes of a piped WAV unfilled, so this test no longer covers that',
        hex(riff_size),
        hex(data_size),
    )
    render = [CONVOLVER, '--filter', str(CROSSOVER / 'filter.cfg'), '--output', '-', '--output-format', 'raw-float']
    refused = subprocess.run([*render, '--input', str(piped)], capture_output=True, check=False)
    message = refused.stderr.decode('utf-8', 'replace')
    assert refused.returncode != 0 and not refused.stdout, (refused.returncode, len(refused.stdout))
    for part in ('never filled in', 'to a pipe', 'Write the file again'):
        assert part in message, (part, message)
    whole = d / 'whole.wav'
    run([*encode, str(whole)])
    from_source = run([*render, '--input', str(source)])
    from_whole = run([*render, '--input', str(whole)])
    assert len(from_whole) == frames * 6 * 4, (len(from_whole) // 24, frames)
    assert from_whole == from_source, 'the WAV FFmpeg wrote to a file rendered differently from its source'
    print(
        f'WAV from an FFmpeg pipe (sizes {riff_size:#x}): refused with the reason; written to a file: {frames} frames'
    )


def main():
    channels, expected = wav_samples(CROSSOVER / 'expected.wav')
    _, source = wav_samples(CROSSOVER / 'input.wav')
    assert channels == 6, channels
    with tempfile.TemporaryDirectory(prefix='convolver-ffmpeg-') as temporary:
        d = Path(temporary)
        movie = film(d / 'movie.mkv')
        output = corrected(movie, d / 'movie-corrected.mkv')
        kinds = [(s['codec_type'], s['codec_name']) for s in streams(output)]
        assert kinds == [('video', 'ffv1'), ('audio', 'flac')], kinds
        assert video(output) == video(movie), 'the video was not copied unchanged'
        track = streams(output)[1]
        assert (int(track['sample_rate']), track['channels']) == (48000, 6), track
        # Every frame, with the filter's tail: the expected output is longer
        # than the input.
        samples = audio(output)
        assert len(expected) // 6 > len(source) // 2, (len(expected), len(source))
        assert len(samples) == len(expected), (len(samples) // 6, len(expected) // 6)
        worst = max(abs(a - b) for a, b in zip(samples, expected))
        peak = max(abs(x) for x in expected)
        assert worst <= TOLERANCE, worst
        print(f'2-in/6-out through FFmpeg: largest difference {worst:.3g} ({worst / peak:.3g} of the peak)')

        # The first track, a 440 Hz tone (and longer), is not what 0:a:1 chose.
        tone = audio(corrected(movie, d / 'tone.mkv', track='0:a:0'))
        assert len(tone) != len(expected), len(tone)
        assert max(abs(a - b) for a, b in zip(tone, expected)) > 0.01

        # Audio that starts 0.5 s into the film starts at time zero afterwards.
        late = film(d / 'late.mkv', audio_offset=0.5)
        starts = [float(s['start_time']) for s in streams(late) if s['codec_type'] == 'audio']
        assert abs(starts[1] - 0.5) < 0.01, starts
        moved = streams(corrected(late, d / 'late-corrected.mkv'))[1]
        assert abs(float(moved['start_time'])) < 0.01, moved['start_time']
        assert len(audio(d / 'late-corrected.mkv')) == len(expected)
        piped_wav(d, len(expected) // 6)
    print('FFmpeg pipe: track selection, 2-in/6-out, frame count and tail, start time, a piped WAV')
    return 0


if __name__ == '__main__':
    sys.exit(main())
