# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared audio fixtures and the direct-convolution oracle for plug-in tests.

One set of synthetic, redistributable fixtures serves the documented first-use
examples (DOC-1) and every plug-in adapter's comparison tests (PLUG-2):

  mono       a mono IR file used directly as the filter
  stereo     a .cfg with two IR channels, cross-feed, a fractional route gain
             and an output delay
  crossover  a .cfg splitting stereo into six outputs (2→6): a three-band
             linear-phase crossover per side, with level trims and delays

Every route has its own response, so a swapped or misrouted channel changes
the output instead of passing by symmetry. Coefficients come from exact
integer arithmetic and are rounded once to float32, so the files are the same
on every platform.

Commands:
  write DIR            regenerate the fixtures into DIR (examples/first-use)
  check DIR            fail if DIR differs from what the generator writes
  run-documented ...   run the documented first-use commands; see run_documented

Only the standard library is used.
"""

import argparse
from pathlib import Path
import math
import re
import shlex
import shutil
import struct
import subprocess
import sys

LATENCY = 256  # Frames every adapter reports; the CLI trims them.
TOLERANCE = 1e-5  # Single-precision FFT convolution against the double oracle.
RATE = 48000
INPUT_FRAMES = 4096
EXPECTED = 'expected.wav'
FIXTURE_NAMES = ('mono', 'stereo', 'crossover')
REPOSITORY = Path(__file__).resolve().parents[2]
DOCUMENTED = (
    REPOSITORY / 'README.md',
    REPOSITORY / 'examples/first-use/README.md',
    REPOSITORY / 'docs/use/first-filter.md',
)
MARKER = '<!-- first-use-example -->'


# --- WAV files -----------------------------------------------------------------


def float_wav_bytes(rate, channels):
    """A canonical 32-bit IEEE float WAV (tag 3, 16-byte fmt chunk); planar input."""
    frames = len(channels[0]) if channels else 0
    data = b''.join(struct.pack(f'<{len(channels)}f', *(c[i] for c in channels)) for i in range(frames))
    fmt = struct.pack('<HHIIHH', 3, len(channels), rate, rate * 4 * len(channels), 4 * len(channels), 32)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt
    body += b'data' + struct.pack('<I', len(data)) + data
    return b'RIFF' + struct.pack('<I', len(body)) + body


def write_float_wav(path, rate, channels):
    Path(path).write_bytes(float_wav_bytes(rate, channels))


def read_float_wav(path, with_rate=False):
    """Planar samples of a 32-bit float WAV, including WAVE_FORMAT_EXTENSIBLE."""
    data = Path(path).read_bytes()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', path
    chunks = {}
    offset = 12
    while offset + 8 <= len(data):
        identifier = data[offset : offset + 4]
        size = struct.unpack('<I', data[offset + 4 : offset + 8])[0]
        chunks[identifier] = data[offset + 8 : offset + 8 + size]
        offset += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = struct.unpack('<HHIIHH', chunks[b'fmt '][:16])
    if tag == 0xFFFE:
        tag = struct.unpack('<H', chunks[b'fmt '][24:26])[0]
    assert tag == 3 and bits == 32, f'{path}: expected 32-bit float, got tag {tag}, {bits} bits'
    samples = struct.unpack(f'<{len(chunks[b"data"]) // 4}f', chunks[b'data'])
    planar = [list(samples[c::channels]) for c in range(channels)]
    return (planar, rate) if with_rate else planar


def to_float32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


# --- Signals and filters --------------------------------------------------------


def noise(frames, seed):
    """Deterministic white noise in [-0.5, 0.5) without depending on random's algorithm."""
    state = seed
    values = []
    for _ in range(frames):
        state = (state * 6364136223846793005 + 1442695040888963407) % 2**64
        values.append((state >> 40) / 2**24 - 0.5)
    return values


def sparse_ir(length, taps):
    ir = [0.0] * length
    for position, gain in taps:
        ir[position] = gain
    return ir


def binomial(order, length, centre):
    """A binomial (near-Gaussian) low-pass of the given order, centred in length.

    Computed exactly as integers over 2**order, then rounded once to float32;
    taps below 2**-24 (-144 dB) are zero so the filter has compact support.
    """
    taps = [0.0] * length
    for k in range(order + 1):
        value = to_float32(math.comb(order, k) / 2**order)
        if abs(value) >= 2**-24:
            taps[centre - order // 2 + k] = value
    return taps


# --- The oracle -------------------------------------------------------------------


def tail(paths, input_delays, output_delays):
    """FIR and routing support after the last input frame (excluding latency)."""
    result = 0
    for path in paths:
        for source, in_gain in path['inputs']:
            for target, out_gain in path['outputs']:
                if in_gain and out_gain:
                    result = max(result, len(path['ir']) - 1 + input_delays[source] + output_delays[target])
    return result


def render(inputs, paths, outputs, input_delays=None, output_delays=None, latency=0):
    """Direct double-precision convolution; planar inputs, planar outputs.

    paths: dicts with 'ir' (taps), 'inputs' and 'outputs' ([(channel, gain)]),
    summed in order, exactly as a configuration describes them. The result has
    latency + len(input) + tail frames: the CLI's file length when latency is 0.
    """
    input_delays = input_delays or [0] * len(inputs)
    output_delays = output_delays or [0] * outputs
    frames = latency + len(inputs[0]) + tail(paths, input_delays, output_delays)
    result = [[0.0] * frames for _ in range(outputs)]
    for path in paths:
        taps = [(k, v) for k, v in enumerate(path['ir']) if v]
        for source, in_gain in path['inputs']:
            signal = inputs[source]
            for target, out_gain in path['outputs']:
                out = result[target]
                shift = latency + input_delays[source] + output_delays[target]
                gain = in_gain * out_gain
                for k, tap in taps:
                    scale = gain * tap
                    start = shift + k
                    for n, x in enumerate(signal):
                        out[start + n] += scale * x
    return result


def direct_convolution(inputs, routes, frames):
    """Reference for (input, output, sparse taps) routes at the adapters' latency."""
    outputs = [[0.0] * frames for _ in range(max(r[1] for r in routes) + 1)]
    for source, target, taps in routes:
        signal = inputs[source]
        out = outputs[target]
        for position, gain in taps:
            for n in range(max(0, LATENCY + position), min(frames, LATENCY + position + len(signal))):
                out[n] += gain * signal[n - LATENCY - position]
    return outputs


def worst_error(actual, expected):
    """Largest absolute difference; lengths and channel counts must match."""
    assert len(actual) == len(expected), (len(actual), len(expected))
    worst = 0.0
    for a, b in zip(actual, expected):
        assert len(a) == len(b), (len(a), len(b))
        assert all(math.isfinite(v) for v in a)
        worst = max(worst, max((abs(x - y) for x, y in zip(a, b)), default=0.0))
    return worst


# --- The fixtures -----------------------------------------------------------------


def _delay_frames(milliseconds):
    # The parser converts in single precision and truncates; these values are exact.
    return int(milliseconds * RATE / 1000)


def fixtures():
    """Fixture name -> files (name -> bytes) and the oracle's description."""
    result = {}

    # Mono: an IR file is the whole filter. Direct sound, a short reflection and
    # three later echoes of alternating sign.
    mono_ir = sparse_ir(2001, [(0, 0.5), (1, -0.25), (100, 0.25), (700, 0.0625), (2000, -0.125)])
    result['mono'] = dict(
        filter='impulse.wav',
        inputs=1,
        outputs=1,
        files={'impulse.wav': float_wav_bytes(RATE, [mono_ir])},
        paths=[dict(ir=mono_ir, inputs=[(0, 1.0)], outputs=[(0, 1.0)])],
        input_delays=[0],
        output_delays=[0],
    )

    # Stereo: different responses per side, plus left fed into the right output
    # through IR channel 1 at gain 0.5, and the right output delayed by 1 ms.
    left = sparse_ir(3001, [(0, 0.5), (1, -0.25), (100, 0.25), (700, 0.0625), (2000, -0.125)])
    right = sparse_ir(3001, [(0, 0.375), (37, 0.125), (255, -0.25), (256, 0.1875), (3000, 0.03125)])
    stereo_cfg = (
        '// Stereo example: 48 kHz, 2 inputs, 2 outputs, speakers front left/right\n'
        '48000 2 2 3\n'
        '// input delays (ms)\n'
        '0 0\n'
        '// output delays (ms): the right output is 1 ms late\n'
        '0 1\n'
        '// left input through IR channel 0 to the left output\n'
        'room.wav\n0\n0\n0\n'
        '// right input through IR channel 1 to the right output\n'
        'room.wav\n1\n1\n1\n'
        '// cross-feed: route 0.5 is input 0 at gain 0.5, through IR channel 1 to output 1\n'
        'room.wav\n1\n0.5\n1\n'
    )
    result['stereo'] = dict(
        filter='filter.cfg',
        inputs=2,
        outputs=2,
        files={'room.wav': float_wav_bytes(RATE, [left, right]), 'filter.cfg': stereo_cfg.encode('utf-8')},
        paths=[
            dict(ir=left, inputs=[(0, 1.0)], outputs=[(0, 1.0)]),
            dict(ir=right, inputs=[(1, 1.0)], outputs=[(1, 1.0)]),
            dict(ir=right, inputs=[(0, 0.5)], outputs=[(1, 1.0)]),
        ],
        input_delays=[0, 0],
        output_delays=[0, _delay_frames(1)],
    )

    # Crossover (2 -> 6): each side splits into low, mid and high bands that add
    # back to a delayed impulse. Low is a wide binomial low-pass (about 1 kHz at
    # 48 kHz), high is the impulse minus a narrow one (about 5 kHz), mid is the
    # difference of the two. The right side's bands are trimmed by 15/16 and one
    # sample later, so all six IR channels differ. The tweeters are padded by
    # 6 dB with route gains and delayed 0.5 ms with output delays.
    length, centre = 129, 64
    wide, narrow = binomial(128, length, centre), binomial(8, length, centre)
    impulse = sparse_ir(length, [(centre, 1.0)])
    bands = [wide, [n - w for n, w in zip(narrow, wide)], [i - n for i, n in zip(impulse, narrow)]]
    bands = [[to_float32(v) for v in band] + [0.0] for band in bands]
    trimmed = [[0.0] + [to_float32(v * 15 / 16) for v in band[:-1]] for band in bands]
    crossover_irs = bands + trimmed
    crossover_cfg = (
        '// Three-way crossover: 48 kHz, 2 inputs, 6 outputs, no speaker labels\n'
        '// outputs: 0 left low, 1 left mid, 2 left high, 3 right low, 4 right mid, 5 right high\n'
        '48000 2 6 0\n'
        '// input delays (ms)\n'
        '0 0\n'
        '// output delays (ms): tweeters 0.5 ms later\n'
        '0 0 0.5 0 0 0.5\n'
        '// left low, mid and high\n'
        'crossover.wav\n0\n0\n0\n'
        'crossover.wav\n1\n0\n1\n'
        '// route 2.5 is output 2 at gain 0.5 (-6 dB)\n'
        'crossover.wav\n2\n0\n2.5\n'
        '// right low, mid and high\n'
        'crossover.wav\n3\n1\n3\n'
        'crossover.wav\n4\n1\n4\n'
        'crossover.wav\n5\n1\n5.5\n'
    )
    result['crossover'] = dict(
        filter='filter.cfg',
        inputs=2,
        outputs=6,
        files={'crossover.wav': float_wav_bytes(RATE, crossover_irs), 'filter.cfg': crossover_cfg.encode('utf-8')},
        paths=[
            dict(ir=crossover_irs[0], inputs=[(0, 1.0)], outputs=[(0, 1.0)]),
            dict(ir=crossover_irs[1], inputs=[(0, 1.0)], outputs=[(1, 1.0)]),
            dict(ir=crossover_irs[2], inputs=[(0, 1.0)], outputs=[(2, 0.5)]),
            dict(ir=crossover_irs[3], inputs=[(1, 1.0)], outputs=[(3, 1.0)]),
            dict(ir=crossover_irs[4], inputs=[(1, 1.0)], outputs=[(4, 1.0)]),
            dict(ir=crossover_irs[5], inputs=[(1, 1.0)], outputs=[(5, 0.5)]),
        ],
        input_delays=[0, 0],
        output_delays=[0, 0, _delay_frames(0.5), 0, 0, _delay_frames(0.5)],
    )

    for seed_base, (name, fixture) in enumerate(result.items()):
        inputs = [noise(INPUT_FRAMES, 10 * (seed_base + 1) + c) for c in range(fixture['inputs'])]
        inputs = [[to_float32(v) for v in channel] for channel in inputs]
        fixture['input'] = inputs
        fixture['files']['input.wav'] = float_wav_bytes(RATE, inputs)
        expected = render(
            inputs, fixture['paths'], fixture['outputs'], fixture['input_delays'], fixture['output_delays']
        )
        fixture['files'][EXPECTED] = float_wav_bytes(RATE, expected)
    return result


def write(directory):
    for name, fixture in fixtures().items():
        folder = Path(directory) / name
        folder.mkdir(parents=True, exist_ok=True)
        for file, data in fixture['files'].items():
            (folder / file).write_bytes(data)
    print(f'Wrote {len(FIXTURE_NAMES)} fixtures to {directory}')


def check(directory):
    """Committed fixtures must be exactly what the generator writes."""
    problems = []
    for name, fixture in fixtures().items():
        folder = Path(directory) / name
        for file, data in fixture['files'].items():
            path = folder / file
            if not path.is_file():
                problems.append(f'{path}: missing')
            elif path.read_bytes() != data:
                problems.append(f'{path}: differs from the generator')
        extra = {p.name for p in folder.iterdir()} - set(fixture['files']) if folder.is_dir() else set()
        problems.extend(f'{folder / e}: not written by the generator' for e in sorted(extra))
    if problems:
        sys.exit('\n'.join(problems) + '\nRegenerate with: python3 tests/plugins/fixtures.py write examples/first-use')
    print(f'{len(FIXTURE_NAMES)} committed fixtures match the generator')


# --- Documented commands ------------------------------------------------------------


def documented_commands(pages=DOCUMENTED):
    """Shell commands in ```sh blocks that follow the first-use marker."""
    commands = []
    for page in pages:
        text = page.read_text(encoding='utf-8')
        blocks = re.findall(re.escape(MARKER) + r'\s*```sh\n(.*?)\n```', text, re.S)
        if not blocks:
            raise ValueError(f'{page}: no first-use examples marked with {MARKER}')
        for block in blocks:
            for line in block.splitlines():
                if line.strip() and not line.lstrip().startswith('#'):
                    commands.append((page, shlex.split(line)))
    return commands


def run_documented(cli, work, examples):
    """Run each documented command as written, from a copy of the repository's
    examples, and compare every rendered file with its fixture's expected.wav.

    Leaves the renders in WORK and lists them in WORK/fixtures.txt (one line per
    fixture: name, filter, input, CLI render, tab separated) for the adapter
    comparisons, which must reproduce the CLI exactly once latency is aligned.
    """
    work = Path(work)
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(examples, work / 'examples' / 'first-use')
    renders = {}
    worst = 0.0
    for page, tokens in documented_commands():
        if tokens[0] != './build/convolver':
            raise ValueError(f'{page}: first-use commands start with ./build/convolver: {tokens}')
        print('+', shlex.join(tokens), flush=True)
        subprocess.run([str(cli), *tokens[1:]], cwd=work, check=True)
        if '--output' not in tokens:
            continue
        options = dict(zip(tokens[1::2], tokens[2::2]))
        unknown = set(options) - {'--filter', '--input', '--output'}
        if unknown:
            raise ValueError(f'{page}: first-use renders use only --filter, --input and --output')
        filter_path = Path(options['--filter'])
        name = filter_path.parent.name
        if filter_path.parent != Path('examples/first-use') / name or name not in FIXTURE_NAMES:
            raise ValueError(f'{page}: {filter_path} is not a first-use fixture')
        rendered = read_float_wav(work / options['--output'])
        expected = read_float_wav(work / filter_path.parent / EXPECTED)
        error = worst_error(rendered, expected)
        print(
            f'  {name}: {len(rendered)} channels x {len(rendered[0])} frames, '
            f'worst error against the oracle {error:.3g}'
        )
        if error > TOLERANCE:
            raise AssertionError(f'{name}: error {error} exceeds {TOLERANCE}')
        worst = max(worst, error)
        renders[name] = (work / filter_path, work / options['--input'], work / options['--output'])
    missing = set(FIXTURE_NAMES) - set(renders)
    if missing:
        raise ValueError(
            f'no documented render for {sorted(missing)}; document one in {DOCUMENTED[1].relative_to(REPOSITORY)}'
        )
    with open(work / 'fixtures.txt', 'w', encoding='utf-8', newline='\n') as listing:
        for name in FIXTURE_NAMES:
            listing.write('\t'.join([name, *(str(p) for p in renders[name])]) + '\n')
    print(f'Documented first-use commands passed; worst error against the oracle {worst:.3g}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('write').add_argument('directory', type=Path)
    commands.add_parser('check').add_argument('directory', type=Path)
    run = commands.add_parser('run-documented')
    run.add_argument('--cli', type=Path, required=True)
    run.add_argument('--work', type=Path, required=True)
    run.add_argument('--examples', type=Path, default=REPOSITORY / 'examples/first-use')
    args = parser.parse_args()
    if args.command == 'write':
        write(args.directory)
    elif args.command == 'check':
        check(args.directory)
    else:
        run_documented(args.cli.resolve(), args.work.resolve(), args.examples.resolve())


if __name__ == '__main__':
    main()
