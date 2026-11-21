# SPDX-License-Identifier: GPL-2.0-or-later
"""Render the first-use fixtures through the installed Audio Unit with AVAudioEngine.

  avaudioengine_tests.py <convolver-preset>

AVAudioEngine and AVAudioUnitEffect are Apple's own hosting stack, which
other Apple-platform hosts build on, and share no code with Convolver's tests
or with pluginval. avaudioengine_render.swift renders each case offline at
block sizes that are not multiples of the engine's 64-frame first block, with
the filter loaded as an .aupreset through ClassInfo. The output, less the
latency the unit reports, must match the fixture's expected output from the
direct double-precision oracle.

The first three cases (AU-15) restore the preset before the engine starts, at
the default Latency. The others (AU-18) follow the hosts' other orders of
events: GarageBand's, which starts the unit at its defaults and restores the
project while it runs, at its fixed 44.1 kHz on a mono track; a Latency
chosen while the engine runs, which the unit must report and tell the host's
property listener of; and a project's round trip, a second unit restored from
the ClassInfo the first one saved, Latency included.

Needs macOS with the component installed for the user (the Audio Unit
workflow installs it) and Xcode's swiftc.
"""

import json
import pathlib
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / 'tests' / 'plugins'))
from fixtures import LATENCY, TOLERANCE, read_float_wav, worst_error, write_float_wav  # noqa: E402

EXAMPLES = REPOSITORY / 'examples' / 'first-use'
REGISTRY = json.loads((REPOSITORY / 'plugins' / 'identities.json').read_text(encoding='utf-8'))
CODES = ','.join(REGISTRY['au'][key] for key in ('type', 'subtype', 'manufacturer'))
# Fixture, filter file and block size. None is a multiple of the engine's
# 64-frame first block, so its blocks straddle render calls.
# Fixture, filter file, block size, the rate to play it at (None: the
# fixture's own), the renderer's options for the host's order of events, and
# the latency the unit must then report. A rate other than the fixture's
# plays the same samples, filter included, labelled with that rate, so the
# oracle's output is the same.
CASES = [
    ('mono', 'impulse.wav', 61, None, [], LATENCY),
    ('stereo', 'filter.cfg', 300, None, [], LATENCY),
    ('crossover', 'filter.cfg', 1000, None, [], LATENCY),
    # GarageBand: 44.1 kHz, a mono track, the project restored while running.
    ('mono', 'impulse.wav', 512, 44100, ['--start-first'], LATENCY),
    # GarageBand's order, then a Latency chosen while the project plays.
    ('stereo', 'filter.cfg', 300, None, ['--start-first', '--latency', '1024'], 1024),
    ('crossover', 'filter.cfg', 1000, None, ['--latency', '256'], 256),
    # A project saved and reopened: ClassInfo from one unit restores another.
    ('stereo', 'filter.cfg', 256, None, ['--round-trip', '--latency', '512'], 512),
]


def interleaved(planar):
    frames = len(planar[0])
    return struct.pack(f'<{frames * len(planar)}f', *(channel[i] for i in range(frames) for channel in planar))


def planar(data, channels):
    samples = struct.unpack(f'<{len(data) // 4}f', data)
    return [list(samples[c::channels]) for c in range(channels)]


def render(renderer, preset_tool, directory, number, name, filter_name, block, play_rate, options, latency):
    folder = EXAMPLES / name
    inputs, rate = read_float_wav(folder / 'input.wav', with_rate=True)
    expected = read_float_wav(folder / 'expected.wav')
    filter_path = folder / filter_name
    if play_rate and play_rate != rate:
        # The same filter samples, labelled with the host's rate.
        filter_path = directory / f'{number}-{filter_name}'
        write_float_wav(filter_path, play_rate, read_float_wav(folder / filter_name))
        rate = play_rate
    preset = directory / f'{number}-{name}.aupreset'
    subprocess.run([preset_tool, str(filter_path), str(preset)], check=True)
    source = directory / f'{number}-{name}-input.f32'
    source.write_bytes(interleaved(inputs))
    output = directory / f'{number}-{name}-output.f32'
    frames = latency + len(expected[0])
    command = [renderer, CODES, str(preset), str(source), str(output), str(rate), str(len(inputs))]
    command += [str(len(expected))]
    command += [str(block), str(frames), *options]
    print('+', ' '.join(command), flush=True)
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, check=False)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        sys.exit(f'{name}: the render failed (exit code {result.returncode})')
    lines = result.stdout.splitlines()
    reported = [line.split()[1] for line in lines if line.startswith('latency ') and len(line.split()) == 2]
    assert reported == [str(latency)], f'{name}: the unit reports latency {reported}, expected {latency}'
    if latency != LATENCY and '--round-trip' not in options:
        # A new Latency reaches the host's property listener, on the main thread.
        assert f'latency notified {latency}' in lines, f"{name}: the host's listener was not told of Latency {latency}"
    rendered = planar(output.read_bytes(), len(expected))
    assert all(len(channel) == frames for channel in rendered), f'{name}: short output'
    # Nothing before the latency; then the oracle's output.
    early = worst_error([channel[:latency] for channel in rendered], [[0.0] * latency for _ in expected])
    assert early < TOLERANCE, f'{name}: {early} before the reported latency'
    worst = worst_error([channel[latency:] for channel in rendered], expected)
    print(
        f'{name}: {len(inputs)} in, {len(expected)} out, {len(expected[0])} frames at {rate} Hz, '
        f'block size {block}, {" ".join(options) or "restored before starting"}, latency {latency}: '
        f'worst error {worst:.3g}'
    )
    assert worst < TOLERANCE, f'{name}: error {worst} exceeds {TOLERANCE}'
    return worst


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    preset_tool = sys.argv[1]
    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(temporary)
        renderer = directory / 'avaudioengine_render'
        source = pathlib.Path(__file__).with_name('avaudioengine_render.swift')
        subprocess.run(['xcrun', 'swiftc', '-O', '-o', str(renderer), str(source)], check=True)
        worst = max(render(str(renderer), preset_tool, directory, number, *case) for number, case in enumerate(CASES))
    print(f'AVAudioEngine render: worst error against the oracle {worst:.3g}')


if __name__ == '__main__':
    main()
