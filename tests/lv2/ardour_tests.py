# SPDX-License-Identifier: GPL-2.0-or-later
"""Play Convolver's LV2 plug-in in a headless Ardour session (HOST-5).

  ardour_tests.py <convolver.lv2> <convolver-preset>

Ardour is a full DAW, and its LV2 host is its own code, not lilv's or JUCE's.
For each first-use fixture (examples/first-use: mono 1 in 1 out, stereo
cross-feed 2 in 2 out, the 2 in 6 out crossover) this test:

1. makes an LV2 preset from the fixture's filter with convolver-preset;
2. makes a session with ardour8-new_session, its master bus one channel per
   plug-in output;
3. runs ardour_session.lua in ardour8-lua: on Ardour's Dummy backend, a track
   with the plug-in, the preset loaded from Ardour's preset list, white noise
   recorded onto the track, the region's fades off, the session saved;
4. exports the session with ardour8-export, a separate process that restores
   the saved session, plug-in state included, and renders it offline with
   Ardour's latency compensation;
5. compares the export with the direct double-precision oracle applied to
   the recorded take: the same length (take plus tail), no offset (Ardour
   compensates the reported 256 frames), within 1e-5.

Needs Ardour's headless tools (Ubuntu's `ardour` package: ardour8-lua,
ardour8-new_session, ardour8-export). Ardour's configuration goes to a
temporary folder, not the user's.
"""

import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / 'tests' / 'plugins'))
from fixtures import TOLERANCE, fixtures, read_float_wav, render, tail, worst_error  # noqa: E402

EXAMPLES = REPOSITORY / 'examples' / 'first-use'
SCRIPT = pathlib.Path(__file__).with_name('ardour_session.lua')
REGISTRY = json.loads((REPOSITORY / 'plugins' / 'identities.json').read_text(encoding='utf-8'))
RATE = 48000
RECORD = 24000  # Half a second of the Dummy backend's noise.
MARGIN = 1000  # Frames exported after the filter's tail, which must be silent.
# Ardour's gain stages ramp up over the first frames of an export (by 0.3 %
# after 1000 frames): the take starts later, so the ramp acts on silence.
LEAD = 9600


def tool(name):
    """Ardour's headless tools carry its major version: ardour8-lua, ardour9-lua..."""
    for major in (9, 8):
        path = shutil.which(f'ardour{major}-{name}')
        if path:
            return path
    sys.exit(f"Ardour's headless tool ardour*-{name} is not installed (Ubuntu: apt-get install ardour)")


def plugin_uri(inputs, outputs):
    """The LV2 URI of the discrete layout with these channel counts."""
    for layout in REGISTRY['fixed_layouts']:
        if (layout['inputs'], layout['outputs'], layout['input'], layout['output']) == (
            inputs,
            outputs,
            'discrete',
            'discrete',
        ):
            return REGISTRY['lv2']['base_uri'] + layout['id']
    sys.exit(f'no discrete {inputs} in, {outputs} out layout in plugins/identities.json')


def run(command, environment, timeout=300):
    print('+', ' '.join(str(part) for part in command), flush=True)
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=timeout, check=False)
    sys.stdout.write(result.stdout)
    sys.stdout.write(result.stderr)
    if result.returncode != 0:
        sys.exit(f'{pathlib.Path(command[0]).name} failed with exit code {result.returncode}')
    return result.stdout


def recorded_take(session, name):
    """The recorded channels of the session's one region, in channel order."""
    # The session file was written by Ardour, in this test's own folder.
    root = ET.parse(session / f'{name}.ardour').getroot()  # noqa: S314
    sources = {source.get('id'): source.get('name') for source in root.iter('Source')}
    regions = [region for playlist in root.iter('Playlist') for region in playlist.iter('Region')]
    assert len(regions) == 1, f'expected one region in the playlists, found {len(regions)}'
    region = regions[0]
    assert region.get('start') == 'a0', f'the region starts inside its take: {region.get("start")}'
    channels = []
    while region.get(f'source-{len(channels)}'):
        file = session / 'interchange' / name / 'audiofiles' / sources[region.get(f'source-{len(channels)}')]
        channels.append(read_float_wav(file)[0])
    return channels


def check(tools, environment, directory, plugin_tool, fixture_name, fixture):
    inputs, outputs = fixture['inputs'], fixture['outputs']
    label = f'Convolver {fixture_name}'
    preset = directory / 'lv2' / f'{fixture_name}.preset.lv2'
    run([plugin_tool, '--name', label, EXAMPLES / fixture_name / fixture['filter'], preset], environment)

    name = fixture_name
    session = directory / 'sessions' / name
    run([tools['new_session'], '-m', str(outputs), '-s', str(RATE), session, name], environment)
    filter_tail = tail(fixture['paths'], fixture['input_delays'], fixture['output_delays'])
    settings = {
        'CONVOLVER_SESSION_DIR': str(session),
        'CONVOLVER_SESSION_NAME': name,
        'CONVOLVER_URI': plugin_uri(inputs, outputs),
        'CONVOLVER_PRESET': label,
        'CONVOLVER_INPUTS': str(inputs),
        'CONVOLVER_OUTPUTS': str(outputs),
        'CONVOLVER_FRAMES': str(RECORD),
        'CONVOLVER_TAIL': str(filter_tail + MARGIN),
        'CONVOLVER_LEAD': str(LEAD),
    }
    log = run([tools['lua'], SCRIPT], {**environment, **settings})
    # The region can be a few frames shorter than the take's files.
    recorded = re.search(r'^Recorded (\d+) frames', log, re.MULTILINE)
    assert recorded, f'{name}: the script did not report the recording'
    length = int(recorded[1])

    exported = directory / f'{name}.wav'
    run([tools['export'], '-b', 'float', '-o', exported, session, name], environment)

    take = recorded_take(session, name)
    assert len(take) == inputs, f'{name}: recorded {len(take)} channels, expected {inputs}'
    assert RECORD <= length <= len(take[0]), f'{name}: a region of {length} frames from a take of {len(take[0])}'
    take = [channel[:length] for channel in take]
    rendered = read_float_wav(exported)
    assert len(rendered) == outputs, f'{name}: exported {len(rendered)} channels, expected {outputs}'
    expected = render(take, fixture['paths'], outputs, fixture['input_delays'], fixture['output_delays'])
    # The export covers the session range, give or take a few frames at its
    # end; the margin keeps the filter's whole tail inside it.
    frames = len(rendered[0])
    wanted = LEAD + length + filter_tail + MARGIN
    assert abs(frames - wanted) <= 64, f'{name}: exported {frames} frames, expected about {wanted}'
    expected = [[0.0] * LEAD + channel + [0.0] * (frames - LEAD - len(channel)) for channel in expected]
    worst = worst_error(rendered, expected)
    if worst >= TOLERANCE:
        for c, (got, want) in enumerate(zip(rendered, expected)):
            errors = [abs(a - b) for a, b in zip(got, want)]
            first = next((n for n, e in enumerate(errors) if e >= TOLERANCE), None)
            if first is not None:
                print(f'{name}: output {c} first differs at frame {first}: {got[first]} instead of {want[first]}')
    peak = max(abs(v) for channel in expected for v in channel)
    print(
        f'{name}: {inputs} in, {outputs} out, {len(take[0])} recorded frames, export of {frames}: '
        f'worst error {worst:.3g} (output peak {peak:.3g})'
    )
    assert peak > 0.01, f'{name}: the recorded input is silent'
    assert worst < TOLERANCE, f'{name}: error {worst} exceeds {TOLERANCE}'
    return worst


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    bundle = pathlib.Path(sys.argv[1]).resolve()
    plugin_tool = pathlib.Path(sys.argv[2]).resolve()
    tools = {name: tool(name) for name in ('lua', 'new_session', 'export')}
    worst = 0.0
    # CONVOLVER_ARDOUR_KEEP=DIR keeps the sessions and exports there, to inspect.
    keep = os.environ.get('CONVOLVER_ARDOUR_KEEP')
    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(keep or temporary)
        if keep:
            shutil.rmtree(directory, ignore_errors=True)
            directory.mkdir(parents=True)
        (directory / 'lv2').mkdir()
        (directory / 'sessions').mkdir()
        shutil.copytree(bundle, directory / 'lv2' / bundle.name)
        environment = {
            **os.environ,
            'LV2_PATH': str(directory / 'lv2'),
            'XDG_CONFIG_HOME': str(directory / 'config'),
            'XDG_CACHE_HOME': str(directory / 'cache'),
        }
        for fixture_name, fixture in fixtures().items():
            worst = max(worst, check(tools, environment, directory, plugin_tool, fixture_name, fixture))
    print(f'Ardour: worst error against the oracle {worst:.3g}')


if __name__ == '__main__':
    main()
