# SPDX-License-Identifier: GPL-2.0-or-later
"""Play Convolver's LV2 or VST3 plug-in in a headless Ardour session (HOST-5, HOST-9).

  ardour_tests.py <convolver.lv2 | Convolver.vst3> <convolver-preset>

Ardour is a full DAW, and its LV2 and VST3 hosts are its own code, not
lilv's, Steinberg's or JUCE's. The bundle's name chooses the format. For each
case below (a first-use fixture from examples/first-use: mono 1 in 1 out,
stereo cross-feed 2 in 2 out, the 2 in 6 out crossover; and a Latency) this
test:

1. makes a preset from the fixture's filter with convolver-preset (an LV2
   preset bundle, or a .vstpreset in the folder Ardour lists VST3 presets
   from);
2. makes a session with ardourN-new_session, its master bus one channel per
   plug-in output;
3. runs ardour_session.lua in ardourN-lua: on Ardour's Dummy backend, a track
   with the plug-in, the preset loaded from Ardour's preset list, the case's
   Latency chosen through Ardour's parameter (Ardour must then report it
   without reopening), white noise recorded onto the track, the region's
   fades off, the session saved;
4. reopens the session in a new ardourN-lua process with ardour_reopen.lua:
   the restored plug-in must report the Latency the session was saved with;
5. exports the session with ardourN-export, a separate process that restores
   the saved session, plug-in state and Latency included, and renders it
   offline with Ardour's latency compensation;
6. compares the export with the direct double-precision oracle applied to
   the recorded take: the same length (take plus tail), no offset (Ardour
   compensates the latency the restored plug-in reports, so a session saved
   at Latency 1024 must play in time when reopened), within 1e-5.

Needs Ardour 9's or 8's headless tools (Ubuntu's `ardour` package:
ardour9-lua, ardour9-new_session, ardour9-export, or ardour8-*), and for VST3
Ardour's scanner, ardour-vst3-scanner, which the package installs beside its
libraries. Ardour's configuration and the plug-in's VST3 folder go to a
temporary home folder, not the user's. An export that Ardour kills by a
signal is tried again, and Ardour 9.0's abort as new_session exits is
accepted once the session exists, each with a warning (see `export` and
`known_new_session_abort`, which ardour_harness_tests.py checks).
"""

import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / 'tests' / 'plugins'))
from fixtures import TOLERANCE, fixtures, read_float_wav, render, tail, worst_error  # noqa: E402

EXAMPLES = REPOSITORY / 'examples' / 'first-use'
SCRIPT = pathlib.Path(__file__).with_name('ardour_session.lua')
REOPEN = pathlib.Path(__file__).with_name('ardour_reopen.lua')
REGISTRY = json.loads((REPOSITORY / 'plugins' / 'identities.json').read_text(encoding='utf-8'))
RATE = 48000
VST3_CLASS = 'Convolver'  # The name the VST3 factory gives the processor class.
VST3_CLASS_ID = REGISTRY['vst3']['processor_class_id']  # Ardour finds the plug-in by it.
RECORD = 24000  # Half a second of the Dummy backend's noise.
MARGIN = 1000  # Frames exported after the filter's tail, which must be silent.
# Ardour's gain stages ramp up over the first frames of an export (by 0.3 %
# after 1000 frames): the take starts later, so the ramp acts on silence.
LEAD = 9600
# The Latency menu's entries, in frames, from the one list every format offers.
LATENCIES = [
    int(frames)
    for frames in re.search(
        r'host_latencies\{([^}]*)\}', (REPOSITORY / 'include' / 'convolver' / 'host.hpp').read_text(encoding='utf-8')
    )[1].split(',')
]
# Each fixture at the default Latency, 0; then a session saved at a larger
# Latency, which Ardour must compensate when the export restores it: the
# largest delay a user is likely to choose, and a multichannel one. Ardour's
# LV2 host chooses the layout by URI. Ardour 8 and 9.0 run Convolver's VST3
# only with 2 in, 2 out (HOST-10), feeding a mono track to both inputs, so
# the mono and crossover presets do not fit there and only the stereo cases
# run: Ardour 8 keeps a VST3's default buses, and Ardour 9.0's request for
# the track's channels reaches the plug-in as no channels for a mono track
# (VST3PI::request_bus_layout masks kMono with the default bus width), which
# Convolver refuses, and as at most the default two channels otherwise.
CASES = {
    'LV2': [('mono', 0), ('stereo', 0), ('crossover', 0), ('stereo', 1024), ('crossover', 256)],
    'VST3': [('stereo', 0), ('stereo', 1024)],
}


def tool(name):
    """Ardour's headless tools carry its major version: ardour8-lua, ardour9-lua..."""
    for major in (9, 8):
        path = shutil.which(f'ardour{major}-{name}')
        if path:
            return path
    sys.exit(f"Ardour's headless tool ardour*-{name} is not installed (Ubuntu: apt-get install ardour)")


def scanner(lua_tool):
    """Ardour's VST3 scanner, which its package installs beside its libraries."""
    major = re.match(r'ardour(\d+)-', pathlib.Path(lua_tool).name)[1]
    path = pathlib.Path(f'/usr/lib/ardour{major}/ardour-vst3-scanner')
    if not path.is_file():
        sys.exit(f"Ardour's VST3 scanner {path} is not installed (Ubuntu: apt-get install ardour)")
    return path


def explain_vst3_discovery(directory):
    """For a failure: the scanner's cache files and Ardour's scan log, which decide what it lists."""
    for folder in (directory / 'cache', directory / 'config'):
        for path in sorted(folder.rglob('*')):
            if path.is_file() and (path.suffix == '.v3i' or path.name == 'scan_log'):
                print(f'--- {path.relative_to(directory)} (mtime {path.stat().st_mtime:.3f})')
                print(path.read_text(encoding='utf-8', errors='replace')[:4000])
    for module in sorted((directory / 'home' / '.vst3').rglob('*.so')):
        print(f'--- {module.relative_to(directory)} (mtime {module.stat().st_mtime:.3f})')


def show_convolver_log(directory):
    """For a failure: the Convolver log in the test's home folder, which explains a refused layout."""
    path = directory / 'home' / '.local' / 'state' / 'convolver' / 'convolver.log'
    if path.is_file():
        print(f'--- {path.relative_to(directory)}')
        print(path.read_text(encoding='utf-8', errors='replace')[-8000:])
    else:
        print(f'--- no Convolver log at {path.relative_to(directory)}')


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


def run(command, environment, timeout=300, *, check=True):
    print('+', ' '.join(str(part) for part in command), flush=True)
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=timeout, check=False)
    sys.stdout.write(result.stdout)
    sys.stdout.write(result.stderr)
    if check and result.returncode != 0:
        sys.exit(f'{pathlib.Path(command[0]).name} failed with exit code {result.returncode}')
    return result


# Ardour 8.4's export sometimes corrupts its heap and aborts at the end
# ("double free or corruption", in ExportGraphBuilder::reset or
# ~PortExportChannel) when a track has a plug-in. On 2026-11-06, with the
# mono session: 4 of 40 exports with Convolver, 3 of 30 with Ardour's own
# a-delay plug-in instead, 3 of 30 with Convolver and no filter loaded, none
# of 40 with no plug-in. valgrind found no error in Convolver. The corrupted
# heap can also leave the export hung (1 in about 100), so a hung export is
# stopped and tried again too. An export takes a few seconds.
EXPORT_ATTEMPTS = 3
EXPORT_TIMEOUT = 120


def warn(message, title='Ardour export'):
    print(f'warning: {message}', flush=True)
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        print(f'::warning title={title}::{message}', flush=True)


def ardour_version(lua_tool):
    """Ardour's version as (major, minor), from `ardourN-lua --version`, or None."""
    result = subprocess.run([lua_tool, '--version'], capture_output=True, text=True, timeout=60, check=False)
    match = re.search(r'version (\d+)\.(\d+)', result.stdout + result.stderr)
    return (int(match[1]), int(match[2])) if match else None


# Ardour 9.0's new_session (Ubuntu 26.04's 9.0.0~ds) writes the session and
# prints that it did, then aborts as it exits with glibc's message about an
# exception not rethrown (a thread's cancellation unwind swallowed during
# teardown). In CI on 2026-11-20 it did so for 7 of 11 sessions in five
# runs, before Convolver is involved. Only that abort is accepted (HOST-7):
# on Ardour 9.0, with that message after the one saying the session was
# created, and the session file present. Any other abort, or the same on
# another version, fails. The Lua script then opens the session, so a
# damaged one fails there. HOST-8 removes this once a fixed Ardour ships.
NEW_SESSION_CREATED = 'Created session in'
NEW_SESSION_ABORT = 'FATAL: exception not rethrown'
NEW_SESSION_ABORTS_IN = {(9, 0)}


def known_new_session_abort(returncode, output, version, session_saved):
    """Whether a failed new_session is Ardour 9.0's abort as it exits, after creating the session."""
    created = output.find(NEW_SESSION_CREATED)
    aborted = output.find(NEW_SESSION_ABORT, created + 1) if created >= 0 else -1
    return (
        returncode == -signal.SIGABRT and version in NEW_SESSION_ABORTS_IN and session_saved and aborted > created >= 0
    )


def new_session(tool_path, environment, session, name, outputs, version):
    """Make the session; accept only Ardour 9.0's known abort at exit once it has created the session."""
    command = [tool_path, '-m', str(outputs), '-s', str(RATE), session, name]
    result = run(command, environment, check=False)
    if result.returncode == 0:
        return
    tool_name = pathlib.Path(tool_path).name
    saved = (session / f'{name}.ardour').is_file()
    if known_new_session_abort(result.returncode, result.stdout + result.stderr, version, saved):
        warn(
            f'{name}: {tool_name} aborted as it exited ({NEW_SESSION_ABORT}), after creating the session, '
            'as Ardour 9.0 can',
            'Ardour new session',
        )
        return
    sys.exit(f'{tool_name} failed with exit code {result.returncode}')


def export(tool_path, environment, exported, session, name):
    """Export the session; an export killed by a signal, or hung, is tried again with a warning."""
    tool_name = pathlib.Path(tool_path).name
    for attempt in range(1, EXPORT_ATTEMPTS + 1):
        exported.unlink(missing_ok=True)  # Nothing from an attempt that died.
        command = [tool_path, '-b', 'float', '-o', exported, session, name]
        try:
            returncode = run(command, environment, timeout=EXPORT_TIMEOUT, check=False).returncode
        except subprocess.TimeoutExpired:
            returncode, failure = None, f'did not finish in {EXPORT_TIMEOUT} s'
        else:
            if returncode >= 0:
                break
            failure = f'died by {signal.Signals(-returncode).name}'
        if attempt < EXPORT_ATTEMPTS:
            warn(
                f'{name}: {tool_name} {failure} (attempt {attempt} of {EXPORT_ATTEMPTS}), '
                f"as Ardour's export can with any plug-in; trying again"
            )
    if returncode is None:
        sys.exit(f'{name}: {tool_name} {failure}, {EXPORT_ATTEMPTS} times')
    if returncode != 0:
        sys.exit(f'{name}: {tool_name} failed with exit code {returncode}')


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


def region_position(session, name):
    """The one region's position as the session file writes it, for diagnostics."""
    root = ET.parse(session / f'{name}.ardour').getroot()  # noqa: S314
    return next(region.get('position') for playlist in root.iter('Playlist') for region in playlist.iter('Region'))


def best_lag(got, want, search=512, span=4096):
    """For a failure: the shift, in frames, at which `got` best matches `want` after the lead."""

    def error(lag):
        frames = range(LEAD, min(LEAD + span, len(want), len(got) - lag))
        return max(abs(got[n + lag] - want[n]) for n in frames)

    return min(((lag, error(lag)) for lag in range(-search, search + 1)), key=lambda pair: pair[1])


def preset_path(directory, format_name, fixture_name, label):
    """Where Ardour lists the preset: LV2_PATH for LV2; the vendor's and plug-in's VST3 preset folder."""
    if format_name == 'LV2':
        return directory / 'lv2' / f'{fixture_name}.preset.lv2'
    return directory / 'home' / '.vst3' / 'presets' / REGISTRY['vendor']['name'] / VST3_CLASS / f'{label}.vstpreset'


def check(tools, environment, directory, plugin_tool, format_name, fixture_name, fixture, latency):
    inputs, outputs = fixture['inputs'], fixture['outputs']
    label = f'Convolver {fixture_name}'
    preset = preset_path(directory, format_name, fixture_name, label)
    if not preset.exists():
        preset.parent.mkdir(parents=True, exist_ok=True)
        run([plugin_tool, '--name', label, EXAMPLES / fixture_name / fixture['filter'], preset], environment)

    name = f'{fixture_name}-{latency}'
    session = directory / 'sessions' / name
    new_session(tools['new_session'], environment, session, name, outputs, tools['version'])
    filter_tail = tail(fixture['paths'], fixture['input_delays'], fixture['output_delays'])
    settings = {
        'CONVOLVER_ARDOUR_MAJOR': re.match(r'ardour(\d+)-', pathlib.Path(tools['lua']).name)[1],
        'CONVOLVER_SESSION_DIR': str(session),
        'CONVOLVER_SESSION_NAME': name,
        'CONVOLVER_FORMAT': format_name,
        'CONVOLVER_PLUGIN': plugin_uri(inputs, outputs)
        if format_name == 'LV2'
        else REGISTRY['vst3']['processor_class_id'],
        'CONVOLVER_PRESET': label,
        'CONVOLVER_LATENCY': str(latency),
        'CONVOLVER_LATENCIES': ','.join(str(frames) for frames in LATENCIES),
        'CONVOLVER_INPUTS': str(inputs),
        'CONVOLVER_OUTPUTS': str(outputs),
        'CONVOLVER_FRAMES': str(RECORD),
        'CONVOLVER_TAIL': str(filter_tail + MARGIN),
        'CONVOLVER_LEAD': str(LEAD),
    }
    result = run([tools['lua'], SCRIPT], {**environment, **settings}, check=False)
    if result.returncode != 0:
        if format_name == 'VST3':
            explain_vst3_discovery(directory)
        sys.exit(f'{name}: {pathlib.Path(tools["lua"]).name} failed with exit code {result.returncode}')
    log = result.stdout
    # HOST-10: Ardour must run the plug-in with the track's channels; a mono
    # track fed to a 2-in VST3, as Ardour 8 and 9.0 do, leaves a mono preset
    # unsuited and the export silent.
    channels = re.search(r'^Plug-in channels: (\d+) in, (\d+) out', log, re.MULTILINE)
    assert channels, f"{name}: the script did not report the plug-in's channels"
    if (int(channels[1]), int(channels[2])) != (inputs, outputs):
        show_convolver_log(directory)  # A refused layout is explained there.
    assert (int(channels[1]), int(channels[2])) == (inputs, outputs), (
        f'{name}: Ardour runs the plug-in with {channels[1]} in, {channels[2]} out, '
        f"not the track's {inputs} in, {outputs} out"
    )
    # The region can be a few frames shorter than the take's files.
    recorded = re.search(r'^Recorded (\d+) frames', log, re.MULTILINE)
    assert recorded, f'{name}: the script did not report the recording'
    length = int(recorded[1])

    # A new process reopens the saved session: the restored plug-in must
    # report the Latency it was saved with, not the default.
    log = run([tools['lua'], REOPEN], {**environment, **settings}).stdout
    reopened = re.search(r'^Reopened: Ardour reports latency (\d+)', log, re.MULTILINE)
    assert reopened, f'{name}: the reopening script did not report the latency'
    assert int(reopened[1]) == latency, f'{name}: reopened with latency {reopened[1]}, saved with {latency}'

    exported = directory / f'{name}.wav'
    export(tools['export'], environment, exported, session, name)

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
                lag, error = best_lag(got, want)
                print(f'{name}: output {c} matches best {lag:+d} frames from where expected (error {error:.3g})')
        print(f'{name}: the region is at {region_position(session, name)} in the session file')
    peak = max(abs(v) for channel in expected for v in channel)
    print(
        f'{format_name} {name}: {inputs} in, {outputs} out, Latency {latency}, {len(take[0])} recorded frames, '
        f'export of {frames}: worst error {worst:.3g} (output peak {peak:.3g})'
    )
    assert peak > 0.01, f'{name}: the recorded input is silent'
    assert worst < TOLERANCE, f'{name}: error {worst} exceeds {TOLERANCE}'
    return worst


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    bundle = pathlib.Path(sys.argv[1]).resolve()
    plugin_tool = pathlib.Path(sys.argv[2]).resolve()
    format_name = {'.lv2': 'LV2', '.vst3': 'VST3'}.get(bundle.suffix)
    if not format_name:
        sys.exit(f'{bundle.name}: name an LV2 bundle (.lv2) or a VST3 bundle (.vst3)')
    tools = {name: tool(name) for name in ('lua', 'new_session', 'export')}
    tools['version'] = ardour_version(tools['lua'])
    print(f'Ardour version: {tools["version"]}', flush=True)
    worst = 0.0
    # CONVOLVER_ARDOUR_KEEP=DIR keeps the sessions and exports in DIR, a new or
    # empty folder, to inspect.
    keep = os.environ.get('CONVOLVER_ARDOUR_KEEP')
    folder = pathlib.Path(keep) if keep else None
    if folder and folder.exists() and (not folder.is_dir() or any(folder.iterdir())):
        sys.exit(f'CONVOLVER_ARDOUR_KEEP={keep} is not an empty folder; name a new or empty folder')
    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(keep or temporary)
        directory.mkdir(parents=True, exist_ok=True)
        (directory / 'lv2').mkdir()
        (directory / 'sessions').mkdir()
        (directory / 'home').mkdir()
        environment = {
            **os.environ,
            # Ardour lists VST3 plug-ins and their presets under ~/.vst3.
            'HOME': str(directory / 'home'),
            'LV2_PATH': str(directory / 'lv2'),
            'XDG_CONFIG_HOME': str(directory / 'config'),
            'XDG_CACHE_HOME': str(directory / 'cache'),
        }
        if format_name == 'LV2':
            shutil.copytree(bundle, directory / 'lv2' / bundle.name)
        else:
            installed = directory / 'home' / '.vst3' / bundle.name
            shutil.copytree(bundle, installed)
            # Ardour trusts a scanner's cache only when the module's mtime is
            # older than the cache file's, in whole seconds
            # (vst3_valid_cache_file in Ardour's libs/ardour/vst3_scan.cc). A
            # bundle built within the same second as the scan is treated as
            # changed, so Ardour lists nothing: date the copy a minute before the scan.
            past = time.time() - 60
            for path in [installed, *installed.rglob('*')]:
                os.utime(path, (past, past), follow_symlinks=False)
            # Ardour looks for VST3 bundles on VST3_PATH and in ~/.vst3, and
            # lists only those its scanner has indexed; the GUI runs it when
            # it finds a new bundle, the headless tools do not.
            environment['VST3_PATH'] = str(installed.parent)
            scan = scanner(tools['lua'])
            library = {'LD_LIBRARY_PATH': ':'.join(filter(None, [str(scan.parent), os.environ.get('LD_LIBRARY_PATH')]))}
            run([scan, '--force', installed], {**environment, **library})
        all_fixtures = fixtures()
        for fixture_name, latency in CASES[format_name]:
            worst = max(
                worst,
                check(
                    tools,
                    environment,
                    directory,
                    plugin_tool,
                    format_name,
                    fixture_name,
                    all_fixtures[fixture_name],
                    latency,
                ),
            )
    print(f'Ardour ({format_name}): worst error against the oracle {worst:.3g}')


if __name__ == '__main__':
    main()
