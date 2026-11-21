# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a finished candidate run and prepare its draft pre-release (REL-8).

Reads the run's description (`gh api repos/OWNER/NAME/actions/runs/ID`), the
run's `complete-preview-candidate` files and a checkout of the run's tag.
Fails, naming the problem, unless the run is a successful manual run of the
candidate workflow on a vX.Y.Z tag, the tag's VERSION and commit match the
candidate, and every file matches SHA256SUMS and passes the developer-preview
integrity gate. Then writes the release notes, the title and SHA256SUMS for
the files to attach: the published platforms' ZIPs and installers and the
corresponding source. It never creates or changes a release; the
"Draft preview release" workflow does that, as a draft, for jrp to publish.
See docs/develop/releasing.md#publishing-an-unsigned-preview.
"""

import argparse
import json
import re
import subprocess
from pathlib import Path

from contract import load_policy, sha256
from promotion_gate import check

# Every platform the candidate run builds: the first preview was macOS only,
# and Windows and Linux joined from the next one (REL-9, jrp's decision of
# 8 November 2026 in docs/project/decisions.md).
PUBLISHED_PLATFORMS = ('macos-arm64', 'macos-x86_64', 'windows-x86_64', 'linux-x86_64')
AUDIENCE = 'macOS 14 or later, Windows 11 and Linux (x86-64, glibc 2.35 or later)'
CANDIDATE_WORKFLOW = '.github/workflows/release.yml'
# The preview published before this one, and what changed for users since,
# in their words; each preview's release-notes change updates both
# (docs/develop/releasing.md#publishing-an-unsigned-preview). The changelog
# holds every change.
PREVIOUS_PREVIEW = '0.14.0'
CHANGES = (
    (
        "A filter file chosen in Convolver's window before the host plays is now kept and saved with the project. "
        'Until the host plays audio through the plug-in, Status says "Filter selected. Waiting for the host\'s audio '
        'settings." and the filter is checked against the rate and channels the host then plays at, rather than '
        'against a brief start at a placeholder rate. In Audacity 4, which starts the plug-in at 44.1 kHz until it '
        "first plays, whatever the project's rate, 0.14.0 refused a 48 kHz filter chosen before playback and saved "
        'that failure, so exports were dry or silent. While the host plays, a file that does not suit it is still '
        'refused and the working filter keeps playing. VST3 and the Audio Unit.'
    ),
    (
        "A configuration list needs the host's rate and channels to choose its entry. Chosen before the host has "
        'played, it is refused with "Play audio once so Convolver can check the host\'s sample rate and channel '
        'counts, then choose the configuration list again." With the window open, it now loads by itself, as if '
        'chosen, as soon as the host plays, and the project is marked changed. Once Convolver has seen the host '
        'play, Status and the window say "The host\'s settings are now known (48000 Hz, 2 in, 2 out): choose the '
        'configuration list again." instead of still calling them unknown. A brief start without audio, such as '
        "Audacity's after a parameter edit, does not count. The window no longer suggests dropping a file beside "
        'every refusal.'
    ),
    (
        'A chosen file now waits for audio in the current start. The VST3 plug-in could refuse a file chosen just '
        'after the host stopped and started again, judging it by the audio of the earlier start; the Audio Unit '
        'counted a render of no frames as playing, so it could refuse a file that should wait, and refused a '
        '48 kHz project restored after a 44.1 kHz probe (error -10851).'
    ),
    (
        "A filter that does not suit the host's sample rate or channels, as a preset, an IR file or a `.cfg`, is now "
        'refused only while another filter that suits the host is playing. With nothing playing it is kept and saved '
        "with the project, plays silence (or, where the channel counts are equal, Mix's dry share or bypass) with "
        'the reason in Status, and plays when the host next starts the plug-in '
        'at a rate and channels that suit it. In Ardour 8.4, a filter chosen just before Ardour restarted the '
        'plug-in with the outputs set in Pin Connections used to be refused, and the export was silent. A filter '
        'refused while another plays stays refused, and restarting the host does not switch to it. A configuration '
        'list with no entry for the host is still refused. VST3 and the Audio Unit; LV2 refuses as before.'
    ),
    (
        'Status also says when a setting does not play as chosen, as the window already did: a Latency not in use '
        'yet ("Latency 1024 frames not in use yet."), Gain mode Estimate without an estimate for the filter ("No '
        'estimate: No clipping\'s gain plays."), and a filter with no gain figure. VST3 and the Audio Unit; in LV2 '
        "the host's log says so."
    ),
    (
        "In Audacity, Convolver's Choose… and Recent have been checked beside Audacity's own preset menu, on a "
        'candidate build before this version (0.16.14, Audacity 4.0.2 on macOS, 21 November 2026): a filter chosen '
        'before playing reached the export, and the next export after saving, quitting and reopening; a list chosen '
        'after one playback took its 48 kHz entry. All four exports matched the reference within -137 dB. Later '
        'changes to choosing a file, listed here, have not yet been checked in Audacity.'
    ),
    (
        "Once the host stops the plug-in, the tail it reports, and the Ring-out row in the window's details, are "
        "the current filter's own. While audio plays it stays the longest tail used since the start, so a host "
        'keeps rendering a tail that is still ringing; before, after a stop it stayed at the longest one the last '
        'start had played.'
    ),
    (
        'In LV2, choosing again the file of the filter already playing keeps it playing, with its tail ringing on, '
        'as the VST3 and Audio Unit windows already did. The decoded filter decides, not the file name: a file of '
        'the same name whose contents changed loads as before.'
    ),
    (
        "With bypass on, Status, the window's report and the Convolver log say what bypass plays instead of "
        'describing the filter: for example "No filter. Bypass is on: audio passes unchanged.", or "Load failed. '
        'Bypass is on: only the dry signal plays." for a kept filter that does not suit the host, where Status '
        'showed "Silent:" or a level the input was not played at. The audio is unchanged.'
    ),
    (
        'The Audio Unit names restored state that brings no name of its own (an "Untitled" preset) after the same '
        'filter\'s earlier file or preset name, as VST3 already did, instead of "Restored filter".'
    ),
    (
        "Choosing a new Latency while audio plays no longer holds the host's control call until the audio thread "
        "finishes its block, in VST3 and the Audio Unit. The Audio Unit's window no longer waits for a load or a "
        'Latency change before showing its first report, and its report follows a bypass switched meanwhile. What '
        'plays and the gap while the new size fills are unchanged.'
    ),
    (
        "When memory runs out while applying a new Latency, Status and the window's report stay as they were. "
        'Before, Status could show Fault again for faults a later load had cleared, or Active over a filter '
        'playing silence.'
    ),
    (
        'The limits guide now states, from measurements on four machines, what one instance handles: room '
        'correction for up to 8 channels, stereo and true-stereo reverbs up to 60 s (true-stereo 30 s at 96 kHz), '
        'a full 8 × 8 matrix only at 48 kHz, and the memory and preparation time a filter takes.'
    ),
    (
        'The plug-ins guide has an Ardour section: what plays on Strict-I/O and Flexible-I/O tracks in the VST3 and '
        'LV2 plug-ins, and which settings Ardour shows. The user guides no longer contradict the plug-ins: Choose… '
        'opens a Convolver `.vstpreset`, the Windows window and its Copy Diagnostics are described beside the '
        "Mac's, and Gain mode and the Linux LV2 plug-in are listed where they belong."
    ),
    (
        'Behind the scenes: the Mac packages are built with Xcode 26.6 (still for macOS 14 and later) and the Linux '
        'package with GCC 12 (still for glibc 2.35), and every plug-in builds without compiler warnings; output may '
        'differ from 0.14.0 within the tested tolerance. Automated host tests now play the VST3 and LV2 plug-ins in '
        'headless Ardour 9 sessions (mono, stereo, Strict-I/O, saved at Latency 256 and 1024), render the Audio '
        "Unit in GarageBand's order of events, and check that "
        'presets and saved state from the published 0.14.0 still load and render the same, within the tested '
        'tolerance, in the command-line tool, VST3, LV2 and the Audio Unit. A script, run by hand on a Mac, now '
        'runs part of the REAPER check from one command.'
    ),
)
TAG = re.compile(r'v(\d+\.\d+\.\d+)')
DESCRIPTIONS = {
    'macos-arm64': 'macOS 14 or later, Apple silicon',
    'macos-x86_64': 'macOS 14 or later, Intel',
    'windows-x86_64': 'Windows 11, x86-64',
    'linux-x86_64': 'Linux x86-64, glibc 2.35 or later',
}


def check_run(run, tag):
    """The run must be a successful manual run of the candidate workflow on `tag`."""
    problems = []
    if run.get('path', '').split('@')[0] != CANDIDATE_WORKFLOW:
        problems.append(f'run {run.get("id")} is {run.get("path")!r}, not the candidate workflow {CANDIDATE_WORKFLOW}')
    if run.get('event') != 'workflow_dispatch':
        problems.append(f'run {run.get("id")} was started by {run.get("event")!r}, not by hand (workflow_dispatch)')
    if run.get('conclusion') != 'success':
        problems.append(f"run {run.get('id')} finished {run.get('conclusion')!r}, not 'success'")
    if run.get('head_branch') != tag:
        problems.append(f'run {run.get("id")} built {run.get("head_branch")!r}, not the tag {tag}')
    if not TAG.fullmatch(tag):
        problems.append(f'{tag!r} is not a vX.Y.Z tag')
    if problems:
        raise ValueError('; '.join(problems))


def check_sums(root):
    """Every line of SHA256SUMS names a file in `root` with that digest; returns {name: digest}."""
    sums = {}
    for line in (root / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        digest, _, name = line.partition('  ')
        if not re.fullmatch(r'[0-9a-f]{64}', digest) or not name or Path(name).name != name:
            raise ValueError(f'malformed SHA256SUMS line: {line!r}')
        path = root / name
        if not path.is_file():
            raise ValueError(f'SHA256SUMS names {name}, which the candidate does not hold')
        if sha256(path) != digest:
            raise ValueError(f'{name} does not match its SHA256SUMS line')
        sums[name] = digest
    return sums


def select(candidate, platforms):
    """The files to attach, in a stable order: each platform's ZIP and installers, then the source."""
    records = {record['id']: record for record in candidate['platforms']}
    missing = [name for name in platforms if name not in records]
    if missing:
        raise ValueError(f'the candidate has no {", ".join(missing)} package')
    files, sources = [], set()
    for name in platforms:
        record = records[name]
        files += [(record['artifact'], name, 'ZIP archive')]
        files += [(installer, name, 'installer') for installer in record.get('installers', [])]
        sources.add(record['source'])
    if len(sources) != 1:
        raise ValueError('the published platforms must share one corresponding-source archive')
    files.append((sources.pop(), None, 'corresponding source'))
    return files


def notes(version, tag, repository, run, commit, files, sums, qualification):
    base = f'https://github.com/{repository}'
    docs = f'{base}/blob/{tag}/docs/use'
    rows = ''.join(
        f'| `{name}` | {DESCRIPTIONS[platform] if platform else "all platforms"} | {kind} | `{sums[name]}` |\n'
        for name, platform, kind in files
    )
    # GitHub shows each line break in a release's notes, so each paragraph
    # is one line, built here from adjacent strings.
    paragraphs = [
        (
            f'**Unsigned developer preview.** Convolver {version} for {AUDIENCE}: the VST® 3 plug-in, on macOS the '
            'Audio Unit, on Linux the LV2 plug-in, the `convolver` command-line tool, `convolver-preset` and '
            '`convolver-equalizer-apo`, with '
            'example presets and the user guides. It is not signed with an Apple Developer ID or a Windows '
            f'code-signing certificate: read the [install guide]({docs}/install.md#unsigned-previews) before '
            'opening it.'
        ),
        '## Download',
        (
            'Choose the file for your system: on macOS and Windows the installer (`.pkg`, `-setup.exe`) or the ZIP '
            'archive, on Linux the ZIP archive. Check its SHA-256 before opening it: `shasum -a 256 <file>` in '
            "macOS's Terminal, `Get-FileHash <file>` in Windows PowerShell (which prints it in capitals), "
            '`sha256sum <file>` on Linux.'
        ),
        '| File | For | Kind | SHA-256 |\n| --- | --- | --- | --- |\n' + rows.rstrip('\n'),
        '## Install and first use',
        (
            f'- [Install the plug-ins]({docs}/install.md), including '
            f'[opening an unsigned preview]({docs}/install.md#unsigned-previews).\n'
            f'- [Your first sound in a host]({docs}/first-sound-in-a-host.md), then '
            f'[check a stereo filter]({docs}/check-a-stereo-filter.md) or '
            f'[check a crossover]({docs}/check-a-crossover-in-a-host.md).\n'
            f'- [Limits]({docs}/limits.md): what Convolver does not do.'
        ),
        '## What changed',
        f'Since {PREVIOUS_PREVIEW}:',
        '\n'.join(f'- {change}' for change in CHANGES),
        f"Each preview's changes are in the [changelog]({base}/blob/{tag}/CHANGELOG.md).",
        '## Known limitations',
        (
            "- On Linux the VST3 plug-in has no window yet. Load a filter from the host's preset menu "
            '(each example comes with a `.vstpreset`, and `convolver-preset` makes one from your own filter) and set '
            "Gain, Mix and Latency in the host's own controls "
            f'([load a preset]({docs}/first-sound-in-a-host.md#optional-load-a-preset)).\n'
            '- The LV2 plug-in (Linux) has no window: '
            "choose a filter with the host's file chooser for its Filter parameter, where the host shows one, or "
            f'load an LV2 preset made with `convolver-preset` ([plug-in presets]({docs}/presets.md)), and set Gain, '
            "Mix and Latency with the host's own controls. Each channel layout is "
            'its own plug-in. It was checked automatically, with lilv, lv2lint, pluginval and headless Ardour 9 '
            'sessions, and from a source build in PipeWire 1.6.8 on virtual devices; nobody has used it in a '
            'desktop host yet. In hosts built on JUCE, pluginval among them, closing the plug-in waits up to '
            '100 ms for a queued Latency, Gain mode or fault report. Since 0.13.24 this mitigates a host crash '
            'rather than ruling it out: a file request the host works after closing, or a request delayed over '
            "100 ms by another plug-in's work, can still reach the closed plug-in and crash the host.\n"
            '- The Windows window has been checked only automatically, on Windows CI machines: it opens, '
            "and the plug-in validator's window tests pass. Nobody has tried it in a Windows host or with a screen "
            'reader yet.\n'
            '- Unsigned: macOS asks you to confirm that you want to open it, and Windows shows a SmartScreen '
            'warning.\n'
            '- Windows needs the Microsoft Visual C++ 2015-2022 x64 runtime, which most computers already have.\n'
            '- An early preview, published before its last checks. Its new audio engine, which adds no delay at '
            'Latency 0, has been checked automatically, against an exact reference and the command-line tool, '
            'on macOS, Windows and Linux, and in Audacity exports on macOS; nobody has listened to it in a host '
            'yet. With the previous engine '
            '(0.12.12), on one Mac, renders and exports from REAPER and Audacity (VST3) and GarageBand (Audio Unit) '
            'matched the command-line tool, and saved projects reopened with their filter. Not yet checked: '
            'listening in any host, VoiceOver, the latest Audio Unit changes in GarageBand, and anyone outside the '
            'project following the guides. Not yet repeated on this version: in REAPER, a render, save and reopen, '
            'a refused preset while playing and the wrong host rate, the render, save and reopen from one command, '
            'and reopening a project saved with an earlier preview; in GarageBand, presets, bypass, settings, a '
            'Latency change stopped and while playing, Recent and Reload; in Audacity, Choose… before playing, Recent, '
            'and save and reopen. The Windows and Linux packages were '
            'checked only automatically, on CI machines that had never built Convolver: the tools and examples ran '
            "from them, the plug-in's library loaded and listed its classes, and the Windows installer installed "
            'and removed itself. Nobody has used them in a host yet. '
            f'[Status]({base}/blob/{tag}/docs/project/status.md#checks-left-before-publication) lists what is open. '
            'Other hosts are untested.\n'
            "- In Audacity, a filter chosen with Convolver's Choose…, even before playing, is kept for exports and "
            'saved projects, and one chosen from Recent for exports (checked in Audacity 4.0.2 on macOS on 21 '
            "November 2026 with the 0.16.14 candidate, as is Audacity's own preset menu; not yet repeated on this "
            'version). Reload and reloading when the file changes are checked only by automated tests.\n'
            '- In Ardour, the VST3 plug-in on a Flexible-I/O track gets 2 inputs and 2 outputs whatever the track '
            'has, so a mono filter plays silence until its outputs are set by hand in Pin Connections (steps not yet '
            "confirmed in Ardour's window), and a crossover cannot play; nor can a crossover on a Strict-I/O track, "
            "Ardour's default, in either format. "
            'On Linux use the LV2 plug-in on a Flexible-I/O track for a crossover '
            f'([Ardour]({docs}/plugins.md#ardour)).\n'
            '- Checked only by automated tests: new in 0.18, everything under What changed except the Audacity '
            'routes checked on the 0.16.14 candidate; new in 0.14, the quicker restore of the playing filter and '
            'the saving once the sound '
            'has stopped; new in 0.13.20, the Gain mode, a `.vstpreset` chosen in the window, Choose… on a track '
            "with more inputs than the filter's, and a preset or undo that keeps the playing filter.\n"
            '- Since 0.13, Latency starts at 0 frames. Some projects saved with 0.12.12 at its default of 256 frames '
            'open at 0; choose 256 again if you want its delay back '
            f'([projects from earlier versions]({docs}/plugins.md#settings)).\n'
            '- Opening a project in an earlier preview: in VST3, a project or preset saved with this version at '
            'Latency 0, the default, is refused as damaged by the first preview, 0.12.12, which plays silence and '
            'says so in its log, and one saved with Gain mode Estimate or No clipping is refused by every preview '
            'before 0.13.20. In the Audio Unit and LV2, previews before 0.13.20 have no Gain mode, so they play such '
            'a project at its Gain setting, which can be louder. Projects and presets saved with earlier previews '
            'open in this one.'
        ),
        '## Report a problem',
        (
            "Copy Diagnostics from Convolver's window on macOS and Windows, or attach the Convolver log on Linux "
            f'([where it is]({docs}/report-a-problem.md#2-collect-convolvers-messages)). '
            'Say which system, host and version you used, how you loaded the filter '
            "(on macOS and Windows Choose…, Recent or Reload; in LV2 the host's file chooser; anywhere a preset or a "
            'saved project), '
            'and whether the problem is in playback, export or reopening '
            f'([how to report]({docs}/report-a-problem.md)). '
            'To say what worked, what was hard to follow or what you missed, open a '
            f'[Preview feedback]({base}/issues/new?template=preview_feedback.yml) issue.'
        ),
        '## Loading responsiveness',
        qualification,
        '## How it was built',
        (
            f'Built and tested by [run {run["id"]}]({run["html_url"]}) from commit `{commit}` (tag `{tag}`). '
            '`Convolver-source.tar.gz` is '
            'the corresponding source: Convolver with the exact SDK and library sources the packages were built from. '
            'The maintainer keeps the matching debugging symbols.'
        ),
    ]
    return '\n\n'.join(paragraphs) + '\n'


def prepare(run, root, tagged, tag, repository, qualification, output, platforms=PUBLISHED_PLATFORMS):
    check_run(run, tag)
    if not qualification.strip():
        raise ValueError('the release notes need the responsiveness qualification result (REL-6)')
    policy = load_policy(tagged / 'packaging/release-policy.json')
    version = TAG.fullmatch(tag).group(1)
    if policy['version'] != version:
        raise ValueError(f'{tag} holds VERSION {policy["version"]}, not {version}')
    commit = subprocess.run(
        ['git', '-C', str(tagged), 'rev-parse', 'HEAD^{commit}'], capture_output=True, text=True, check=True
    ).stdout.strip()
    if commit != run['head_sha']:
        raise ValueError(f'{tag} is commit {commit}, but run {run["id"]} built {run["head_sha"]}')
    candidate = json.loads((root / 'candidate.json').read_text(encoding='utf-8'))
    if candidate.get('source_commit') != commit:
        raise ValueError(f'the candidate was built from {candidate.get("source_commit")}, not {tag} ({commit})')
    check(candidate, policy, root)
    sums = check_sums(root)
    listed = {asset['name'] for asset in candidate['assets']}
    if set(sums) != listed:
        raise ValueError(f'SHA256SUMS and candidate.json list different files: {sorted(set(sums) ^ listed)}')
    files = select(candidate, platforms)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'notes.md').write_text(
        notes(version, tag, repository, run, commit, files, sums, qualification.strip()), encoding='utf-8'
    )
    (output / 'SHA256SUMS').write_text(''.join(f'{sums[name]}  {name}\n' for name, _, _ in files), encoding='utf-8')
    (output / 'title.txt').write_text(f'Convolver {version} (unsigned developer preview)\n', encoding='utf-8')
    return [name for name, _, _ in files]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--run', type=Path, required=True, help="the run's description, as JSON from the API")
    parser.add_argument('--root', type=Path, required=True, help="the run's complete-preview-candidate files")
    parser.add_argument('--tagged', type=Path, required=True, help='a checkout of the tag')
    parser.add_argument('--tag', required=True)
    parser.add_argument('--repository', required=True, help='OWNER/NAME')
    parser.add_argument('--qualification', required=True, help='the REL-6 result, for the release notes')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run = json.loads(args.run.read_text(encoding='utf-8'))
    names = prepare(run, args.root, args.tagged, args.tag, args.repository, args.qualification, args.output)
    print(f'{args.tag}: run {run["id"]} checked; files to attach:')
    for name in names:
        print('  ' + name)


if __name__ == '__main__':
    main()
