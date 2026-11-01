# SPDX-License-Identifier: GPL-2.0-or-later
"""Run Tracktion's pluginval against the built VST3 bundle.

pluginval is an independent JUCE-based host, so these tests load the plug-in
through code other than the Steinberg SDK that the adapter tests and validator
share.

  validate   <pluginval> <bundle> [extra pluginval options...]
             Host conformance: lifecycle, state, buses, parameter fuzzing and
             allocations during processing, at strictness level 10. On
             macOS, where the plug-in has a window (VST3-11), pluginval's
             editor tests open and close it too; elsewhere they are skipped.
  acceptance <pluginval> <bundle> <preset-tool>
             Loads real filters through the host's state call, renders audio
             at awkward block sizes, and compares the result with a direct
             double-precision convolution. Needs pluginval 2's `test`
             command; with pluginval 1 it is skipped (exit code 77).

pluginval 1 and 2 take different command lines; the version is read from
`pluginval --version`.
"""

import json
import math
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'plugins'))
# The shared PLUG-2 helpers: WAV files, deterministic noise and the direct oracle.
from fixtures import (  # noqa: E402
    LATENCY,
    TOLERANCE,
    direct_convolution,
    noise,
    read_float_wav,
    sparse_ir,
    write_float_wav,
)


def run_pluginval(pluginval, arguments, **kwargs):
    command = [pluginval, *arguments]
    # JUCE needs an X server to load plug-ins on Linux.
    if sys.platform.startswith('linux') and not os.environ.get('DISPLAY'):
        xvfb = shutil.which('xvfb-run')
        if xvfb is None:
            sys.exit('pluginval needs DISPLAY or xvfb-run on Linux')
        command = [xvfb, '--auto-servernum', *command]
    print('+', ' '.join(command), flush=True)
    check = kwargs.pop('check', False)
    return subprocess.run(command, check=check, **kwargs)


SKIPPED = 77  # CTest's SKIP_RETURN_CODE for this test.


def major_version(pluginval):
    """pluginval's major version, from `pluginval --version` ("pluginval - 1.0.4")."""
    output = subprocess.run([pluginval, '--version'], capture_output=True, text=True, timeout=120, check=False).stdout
    match = re.search(r'(\d+)\.\d+', output)
    if not match:
        sys.exit(f'cannot read a version from `{pluginval} --version`: {output.strip()!r}')
    return int(match[1])


def validate_command(major, bundle, extra, platform=sys.platform):
    """pluginval 2 has a `validate` command; pluginval 1 takes options and --validate.

    The editor tests run only on macOS, the one system with a window so far
    (VST3-11; Windows and Linux follow in VST3-12 and VST3-13).
    """
    gui = [] if platform == 'darwin' else ['--skip-gui-tests']
    options = ['--strictness-level', '10', *gui, *extra]
    if major >= 2:
        return ['validate', *options, bundle]
    return [*options, '--validate', bundle]


def component_state(preset):
    """The 'Comp' chunk of a .vstpreset: the plug-in's own component state."""
    data = preset.read_bytes()
    assert data[:4] == b'VST3', preset
    offset = struct.unpack('<q', data[40:48])[0]
    assert data[offset : offset + 4] == b'List', preset
    for index in range(struct.unpack('<i', data[offset + 4 : offset + 8])[0]):
        entry = data[offset + 8 + 20 * index : offset + 28 + 20 * index]
        start, size = struct.unpack('<qq', entry[4:])
        if entry[:4] == b'Comp':
            return data[start : start + size]
    raise AssertionError(f'{preset}: no component state')


def juce_state(component):
    """Wrap component state the way JUCE's VST3 host expects from setStateInformation.

    JUCE reads an XML document (magic 0x21324356, length, NUL-terminated text) whose
    IComponent element holds the bytes in JUCE's own base64 variant. Raw component
    state is silently ignored, leaving the plug-in empty.
    """
    alphabet = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'
    bits = int.from_bytes(component, 'little')
    count = (len(component) * 8 + 5) // 6
    encoded = f'{len(component)}.' + ''.join(alphabet[(bits >> (6 * i)) & 63] for i in range(count))
    text = (
        f'<?xml version="1.0" encoding="UTF-8"?> <VST3PluginState><IComponent>{encoded}</IComponent></VST3PluginState>'
    ).encode() + b'\0'
    return struct.pack('<II', 0x21324356, len(text) - 1) + text


CASES = [
    {
        # Stereo impulse file: each channel filters its own input.
        'name': 'stereo-wav',
        'rate': 48000,
        'block_size': 300,  # Not a multiple of the 256-sample partition.
        'ir': [
            [(0, 0.5), (1, -0.25), (100, 0.25), (700, 0.0625), (2000, -0.125)],
            [(0, 0.375), (37, 0.125), (255, -0.25), (256, 0.1875), (3000, 0.03125)],
        ],
        'filter': 'ir.wav',
        'routes': [(0, 0, 0), (1, 1, 1)],
    },
    {
        # Configuration file with cross-feed: every output mixes both inputs.
        'name': 'crossfeed-cfg',
        'rate': 44100,
        'block_size': 61,  # Smaller than a partition; partitions straddle callbacks.
        'ir': [[(0, 0.75), (5, -0.125), (300, 0.0625)], [(20, 0.25), (513, -0.0625), (1500, 0.03125)]],
        'filter': 'crossfeed.cfg',
        'config': '44100 2 2 0\n0 0\n0 0\nir.wav\n0\n0\n0\nir.wav\n0\n1\n1\nir.wav\n1\n0\n1\nir.wav\n1\n1\n0\n',
        'routes': [(0, 0, 0), (1, 1, 0), (0, 1, 1), (1, 0, 1)],
    },
]


def acceptance(pluginval, bundle, preset_tool):
    seconds = 1.0
    worst_overall = 0.0
    with tempfile.TemporaryDirectory() as directory:
        for case in CASES:
            root = pathlib.Path(directory) / case['name']
            root.mkdir()
            rate = case['rate']
            length = max(p for taps in case['ir'] for p, _ in taps) + 1
            write_float_wav(root / 'ir.wav', rate, [sparse_ir(length, taps) for taps in case['ir']])
            if 'config' in case:
                (root / case['filter']).write_text(case['config'])
            frames = int(rate * seconds)
            inputs = [noise(frames, 1), noise(frames, 2)]
            write_float_wav(root / 'input.wav', rate, inputs)
            preset = root / 'filter.vstpreset'
            subprocess.run([preset_tool, str(root / case['filter']), str(preset)], check=True)
            (root / 'state.bin').write_bytes(juce_state(component_state(preset)))
            render_frames = frames + length + LATENCY
            config = {
                'name': case['name'],
                'input': {'audio': 'input.wav'},
                'reference': 'rendered.wav',
                'state': {'file': 'state.bin'},
                'sample_rate': rate,
                'block_size': case['block_size'],
                'render_duration': render_frames / rate,
                'comparison': {'sample': 0},
            }
            (root / 'case.json').write_text(json.dumps(config, indent=2))

            # The first run records what the host rendered; check that against the oracle.
            run_pluginval(
                pluginval, ['test', '--plugin', str(bundle), '--record-missing', str(root / 'case.json')], check=True
            )
            rendered = read_float_wav(root / 'rendered.wav')
            routes = [(source, target, case['ir'][channel]) for source, target, channel in case['routes']]
            expected = direct_convolution(inputs, routes, len(rendered[0]))
            assert len(rendered) == len(expected), (len(rendered), len(expected))
            assert len(rendered[0]) >= frames + length + LATENCY - 1, len(rendered[0])
            worst = max(abs(a - b) for out, ref in zip(rendered, expected) for a, b in zip(out, ref))
            assert all(math.isfinite(v) for out in rendered for v in out)
            print(
                f'{case["name"]}: {len(rendered)} channels x {len(rendered[0])} frames at '
                f'block size {case["block_size"]}, worst error {worst:.3g}'
            )
            assert worst < TOLERANCE, f'{case["name"]}: error {worst} exceeds {TOLERANCE}'
            worst_overall = max(worst_overall, worst)

            # A second render must reproduce the first exactly (fresh instance, same state).
            run_pluginval(pluginval, ['test', '--plugin', str(bundle), str(root / 'case.json')], check=True)
    print(f'pluginval acceptance: worst error against direct convolution {worst_overall:.3g}')


def main():
    mode = sys.argv[1]
    if mode == 'validate':
        pluginval, bundle, *extra = sys.argv[2:]
        major = major_version(pluginval)
        print(f'pluginval {major}.x', flush=True)
        result = run_pluginval(pluginval, validate_command(major, bundle, extra))
        sys.exit(result.returncode)
    if mode == 'acceptance':
        pluginval, bundle, preset_tool = sys.argv[2:]
        major = major_version(pluginval)
        if major < 2:
            print(
                f'Skipped: pluginval {major} has no `test` command, which renders audio from a preset; '
                'this test needs pluginval 2 (docs/develop/testing.md#pluginval).'
            )
            sys.exit(SKIPPED)
        acceptance(pluginval, pathlib.Path(bundle), preset_tool)
        return
    sys.exit(f'unknown mode {mode}')


if __name__ == '__main__':
    main()
