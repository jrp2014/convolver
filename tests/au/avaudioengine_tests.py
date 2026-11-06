# SPDX-License-Identifier: GPL-2.0-or-later
"""Render the first-use fixtures through the installed Audio Unit with AVAudioEngine.

  avaudioengine_tests.py <convolver-preset>

AVAudioEngine and AVAudioUnitEffect are Apple's own hosting stack, which
other Apple-platform hosts build on, and share no code with Convolver's tests
or with pluginval. avaudioengine_render.swift renders each first-use fixture
(examples/first-use: mono 1 in 1 out, stereo cross-feed 2 in 2 out, the 2 in
6 out crossover) offline at a block size that is not a multiple of the
256-frame partition, with the filter loaded as an .aupreset through
ClassInfo. The output, less the latency the unit reports, must match the
fixture's expected output from the direct double-precision oracle.

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
from fixtures import LATENCY, TOLERANCE, read_float_wav, worst_error  # noqa: E402

EXAMPLES = REPOSITORY / 'examples' / 'first-use'
REGISTRY = json.loads((REPOSITORY / 'plugins' / 'identities.json').read_text(encoding='utf-8'))
CODES = ','.join(REGISTRY['au'][key] for key in ('type', 'subtype', 'manufacturer'))
# Fixture, filter file and block size. 61 is smaller than a partition, so
# partitions straddle render calls; 300 and 1000 are not multiples of 256.
CASES = [('mono', 'impulse.wav', 61), ('stereo', 'filter.cfg', 300), ('crossover', 'filter.cfg', 1000)]


def interleaved(planar):
    frames = len(planar[0])
    return struct.pack(f'<{frames * len(planar)}f', *(channel[i] for i in range(frames) for channel in planar))


def planar(data, channels):
    samples = struct.unpack(f'<{len(data) // 4}f', data)
    return [list(samples[c::channels]) for c in range(channels)]


def render(renderer, preset_tool, directory, name, filter_name, block):
    folder = EXAMPLES / name
    inputs, rate = read_float_wav(folder / 'input.wav', with_rate=True)
    expected = read_float_wav(folder / 'expected.wav')
    preset = directory / f'{name}.aupreset'
    subprocess.run([preset_tool, str(folder / filter_name), str(preset)], check=True)
    source = directory / f'{name}-input.f32'
    source.write_bytes(interleaved(inputs))
    output = directory / f'{name}-output.f32'
    frames = LATENCY + len(expected[0])
    command = [renderer, CODES, str(preset), str(source), str(output), str(rate), str(len(inputs))]
    command += [str(len(expected))]
    command += [str(block), str(frames)]
    print('+', ' '.join(command), flush=True)
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, check=False)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        sys.exit(f'{name}: the render failed (exit code {result.returncode})')
    latency = [line.split()[1] for line in result.stdout.splitlines() if line.startswith('latency ')]
    assert latency == [str(LATENCY)], f'{name}: the unit reports latency {latency}, expected {LATENCY}'
    rendered = planar(output.read_bytes(), len(expected))
    assert all(len(channel) == frames for channel in rendered), f'{name}: short output'
    # Nothing before the latency; then the oracle's output.
    early = worst_error([channel[:LATENCY] for channel in rendered], [[0.0] * LATENCY for _ in expected])
    assert early < TOLERANCE, f'{name}: {early} before the reported latency'
    worst = worst_error([channel[LATENCY:] for channel in rendered], expected)
    print(
        f'{name}: {len(inputs)} in, {len(expected)} out, {len(expected[0])} frames at {rate} Hz, '
        f'block size {block}: worst error {worst:.3g}'
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
        worst = max(render(str(renderer), preset_tool, directory, *case) for case in CASES)
    print(f'AVAudioEngine render: worst error against the oracle {worst:.3g}')


if __name__ == '__main__':
    main()
