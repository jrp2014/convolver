# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a finished candidate run and prepare its draft release (REL-8, REL-1).

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

With --production, for a full release rather than a pre-release (REL-1), the
candidate must also pass every production gate: the requirements and
`production_enabled` come from this checkout's release policy (the workflow's
`main`, so that jrp's current decisions judge any tag), and the evidence from
a release evidence file (promotion_gate.with_evidence). With --dry-run as
well, the gates it does not meet are listed instead of refusing, and the
notes are still written. See docs/develop/releasing.md#production-gates.
"""

import argparse
import json
import re
import subprocess
from pathlib import Path

from contract import load_policy, sha256
from promotion_gate import check, production_problems, with_evidence

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
PREVIOUS_PREVIEW = '0.18.12'
CHANGES = (
    (
        'A mono IR file, or any filter with one input and one output, now filters every channel of stereo or '
        'wider audio on its own, and a four-channel IR file on stereo audio plays as true stereo (left to left, '
        'left to right, right to left, right to right), without a `.cfg`, as other convolution reverbs read these '
        'files. A filter that matches the channels exactly still wins, in a list too, so existing setups play as '
        "before; a file marked as Ambisonic B-format is refused with the reason. Status, the log, the window's "
        'Routing line and the command line say when such a routing plays. In Ardour, a mono filter now plays with '
        'the VST3 plug-in on a Flexible-I/O track without setting its channels by hand.'
    ),
    (
        'In VST3, host presets, window presets and later Latency, Gain mode, Gain and Mix changes now play, show '
        "and save the same in every order of host calls the plug-in's tests try, delivered on time or late: the "
        "plug-in's two halves ask each other directly for settings instead of passing them in messages that a "
        'host could deliver late or in the middle of a restore, and each window pairs only with its own '
        "instance's audio half, even when the host passes the pairing message late. A Latency or Gain mode chosen "
        'just '
        'before a preset is restored no longer overrides that preset when the host delivers it late, which could '
        "play and save the earlier Latency while the window showed the preset's. Settings changed after a refused "
        'preset no longer jump back when the host answers late, a window preset no longer comes back over a later '
        'edit or restore, a refused preset no longer leaves the window showing its Bypass or Status showing '
        "Active, and a host that restores the window's half first, or the same state twice, reads the latency "
        'that plays. No change to how a filter sounds or to the saved-project format.'
    ),
    (
        'A valid VST3 project or preset is no longer refused as "could not be read: std::bad_alloc" when memory '
        'runs short, and restores are quicker, including the one Audacity makes at every playback start, because '
        'they no longer make a second copy of the filter. On Windows, running out of memory while loading a '
        'filter could leave the plug-in not responding; it now reports the failure.'
    ),
    (
        'When a host offers the VST3 plug-in a channel layout it cannot take, as REAPER does for every instance '
        'with 64 inputs and outputs, the Convolver log now says the plug-in declined the offer, and that the next '
        '"Started at" line gives the layout in use, instead of a "Refused" line that looked like a fault.'
    ),
    (
        "On Windows, the note under the Gain row in Estimated or Clip-safe uses the small type of the window's "
        'other notes, so its lines can no longer be cut off.'
    ),
    (
        'Gain mode Estimated, and `--gain-db auto` on the command line, now give a figure for every filter that '
        'Clip-safe can play, including one with taps near the largest number a float holds, the same in every '
        "build, so the plug-ins' Status note `Too loud to estimate` can no longer happen and is gone. Ordinary "
        "filters' figures are unchanged, bit for bit."
    ),
    (
        "The README's new What to expect section says, for room correction and crossovers, music at each track's "
        'own sample rate and a reverb on a send, what works today, the measured load and what you do.'
    ),
)
TAG = re.compile(r'v(\d+\.\d+\.\d+)')
REQUIREMENTS = Path(__file__).resolve().parents[2] / 'packaging/release-policy.json'
SYSTEMS = {'Darwin': 'macOS', 'Windows': 'Windows', 'Linux': 'Linux'}
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


def systems(names):
    """'macOS', 'macOS and Windows', 'macOS, Windows and Linux'."""
    names = list(dict.fromkeys(names))
    return ', '.join(names[:-1]) + (' and ' if len(names) > 1 else '') + names[-1]


def production_lines(policy, docs, base, tag):
    """The opening paragraph's signing sentence and the generated known
    limitations of a full release, from each platform's production rules."""
    platforms = policy['platforms']
    sentence = []
    # Only macOS signing exists (REL-1); Windows signing would need its own words.
    rules = {p['id']: p.get('production', {'signed': False, 'unverified': []}) for p in platforms}
    if any(p['os'] == 'Darwin' and rules[p['id']]['signed'] for p in platforms):
        sentence.append('The macOS files are signed with an Apple Developer ID and notarized.')
    limitations = []
    if any(p['os'] == 'Windows' and not rules[p['id']]['signed'] for p in platforms):
        sentence.append(
            'The Windows files are not signed with a code-signing certificate, so Windows shows a SmartScreen '
            f'warning first: read the [install guide]({docs}/install.md) before opening them.'
        )
        limitations.append(
            '- The Windows files are not signed: Windows shows a SmartScreen warning before the installer, or a '
            'program from the ZIP archive, first runs.'
        )
    unverified = {}
    for platform in platforms:
        for kind in rules[platform['id']]['unverified']:
            unverified.setdefault(kind, []).append(SYSTEMS[platform['os']])
    if 'host' in unverified:
        limitations.append(
            f'- Not verified in a named host on {systems(unverified.pop("host"))}: the packages were checked '
            'automatically, on CI machines that had never built Convolver, and nobody has tested them in a host '
            f'for this release ([status]({base}/blob/{tag}/docs/project/status.md)).'
        )
    for kind, names in unverified.items():
        limitations.append(f'- Not verified on {systems(names)}: {kind.replace("_", " ")}.')
    return ' '.join(sentence), limitations


def preview_checks(base, tag):
    """The known limitation that says what was checked on a preview and what is
    still to retest; the release-notes pull request rewrites it for each preview."""
    return (
        '- An early preview, published before its last checks. Its new audio engine, which adds no delay at '
        'Latency 0, has been checked automatically, against an exact reference and the command-line tool, '
        'on macOS, Windows and Linux, and in Audacity exports on macOS; nobody has listened to it in a host '
        'yet. With the previous engine '
        '(0.12.12), on one Mac, renders and exports from REAPER and Audacity (VST3) and GarageBand (Audio Unit) '
        'matched the command-line tool, and saved projects reopened with their filter. Not yet checked: '
        'listening in any host, VoiceOver, the latest Audio Unit changes in GarageBand, and anyone outside the '
        'project following the guides. Checked on 0.18.10, built from its tag on one Mac (macOS 27.2.1, 22 '
        'November 2026), and not yet repeated on this version: in REAPER 7.86, the render, save and reopen from '
        'one command, stereo and six-channel renders matching after reopening, the ring-out kept through Stop, a '
        '30-second filter at Latency 0 without underruns, and a project saved with 0.14.0 rendering the same in '
        'VST3 and the Audio Unit; in Audacity 4.0.2, a filter chosen with Choose… before playing; in GarageBand '
        '10.4.15, exports aligned exactly at Latency 0 and within 1.2e-7 at 1024. To retest on this preview, '
        'with what 0.18.10 showed where it was tried: in REAPER, a refused preset while playing (0.18.10 refused '
        'it correctly but showed the wrong settings, fixed in 0.18.12) and the wrong host rate; in GarageBand, '
        'presets, bypass, settings, a Latency change stopped and while playing, and the recent files and Reload '
        'in the Choose Filter menu; in Audacity, a configuration list chosen before playing (failed on 0.18.10, '
        'fixed in 0.18.12), the recent files, and save and reopen. The Windows and Linux packages were '
        'checked only automatically, on CI machines that had never built Convolver: the tools and examples ran '
        "from them, the plug-in's library loaded and listed its classes, and the Windows installer installed "
        'and removed itself, on Windows Server 2025 and on Windows 11 on Arm, where the package ran under the '
        'x64 emulation built into Windows (GitHub has no Windows 11 machine for x86-64). Nobody has used them '
        'in a host yet. The macOS packages were '
        'also installed and removed on such machines with macOS 15, the oldest GitHub has; nothing has run on '
        'macOS 14 itself. '
        f'[Status]({base}/blob/{tag}/docs/project/status.md#checks-left-before-publication) lists what is open. '
        'Other hosts are untested.\n'
    )


def notes(version, tag, repository, run, commit, files, sums, qualification, production=None):
    """The release notes; `production` is the release policy for a full release, None for a preview."""
    base = f'https://github.com/{repository}'
    docs = f'{base}/blob/{tag}/docs/use'
    if production:
        signing, generated = production_lines(production, docs, base, tag)
    rows = ''.join(
        f'| `{name}` | {DESCRIPTIONS[platform] if platform else "all platforms"} | {kind} | `{sums[name]}` |\n'
        for name, platform, kind in files
    )
    # GitHub shows each line break in a release's notes, so each paragraph
    # is one line, built here from adjacent strings.
    contents = (
        f'Convolver {version} for {AUDIENCE}: the VST® 3 plug-in, on macOS the '
        'Audio Unit, on Linux the LV2 plug-in, the `convolver` command-line tool, `convolver-preset` and '
        '`convolver-equalizer-apo`, with '
        'example presets and the user guides.'
    )
    if production:
        opening = f'{contents} {signing}'
        install = f'- [Install the plug-ins]({docs}/install.md).\n'
    else:
        opening = (
            f'**Unsigned developer preview.** {contents} It is not signed with an Apple Developer ID or a Windows '
            f'code-signing certificate: read the [install guide]({docs}/install.md#unsigned-previews) before '
            'opening it.'
        )
        install = (
            f'- [Install the plug-ins]({docs}/install.md), including '
            f'[opening an unsigned preview]({docs}/install.md#unsigned-previews).\n'
        )
    paragraphs = [
        opening,
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
            install + f'- [Your first sound in a host]({docs}/first-sound-in-a-host.md), then '
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
        )
        # A full release says what its platforms' production rules leave unsigned
        # or not verified instead.
        + (
            ''.join(line + '\n' for line in generated)
            if production
            else (
                '- Unsigned: macOS asks you to confirm that you want to open it, and Windows shows a SmartScreen '
                'warning.\n'
            )
        )
        + '- Windows needs the Microsoft Visual C++ 2015-2022 x64 runtime, which most computers already have.\n'
        # The release-notes pull request for a full release says what was checked
        # on it; this paragraph is about previews.
        + ('' if production else preview_checks(base, tag))
        + (
            "- In Audacity, a filter chosen with Convolver's Choose File…, even before playing, is kept for exports "
            'and saved projects, and one chosen from its recent files for exports (checked in Audacity 4.0.2 on macOS '
            'on 21 November 2026 with the 0.16.14 candidate, whose window had separate Choose… and Recent buttons; '
            'Choose… before playing was checked again on 0.18.10 on 22 November; '
            "not yet repeated on this version; Audacity's own preset menu "
            'was last checked on 7 November 2026, with a build before the first preview). Reload and reloading when '
            'the file changes are checked only by '
            'automated tests.\n'
            '- In Ardour, the VST3 plug-in on a Flexible-I/O track gets 2 inputs and 2 outputs whatever the track '
            "has; a mono filter plays on each of them (checked by automated tests, not yet in Ardour's window), "
            'but a crossover cannot play; nor can a crossover on a Strict-I/O track, '
            "Ardour's default, in either format. "
            'On Linux use the LV2 plug-in on a Flexible-I/O track for a crossover '
            f'([Ardour]({docs}/plugins.md#ardour)).\n'
            '- Checked only by automated tests: new since 0.18.12, everything under What changed, including the new '
            "way the VST3 plug-in's two halves keep in step, which every VST3 check above predates; new in 0.18.12, "
            f'the changes [its notes]({base}/releases/tag/v0.18.12) list; new in 0.18.10, '
            f'the changes [its notes]({base}/releases/tag/v0.18.10) list, apart from those checked on 0.18.10 '
            'above and '
            'the Audacity routes checked on the 0.16.14 candidate; new in 0.14, '
            'the quicker restore of the playing filter and '
            'the saving once the sound '
            'has stopped; new in 0.13.20, the Gain mode, a `.vstpreset` chosen in the window, choosing a file on a '
            'track '
            "with more inputs than the filter's, and a preset or undo that keeps the playing filter.\n"
            '- Since 0.13, Latency starts at 0 frames. Some projects saved with 0.12.12 at its default of 256 frames '
            'open at 0; choose 256 again if you want its delay back '
            f'([projects from earlier versions]({docs}/plugins.md#settings)).\n'
            '- Opening a project in an earlier preview: in VST3, a project or preset saved with this version at '
            'Latency 0, the default, is refused as damaged by the first preview, 0.12.12, which plays silence and '
            'says so in its log, and one saved with a Gain mode other than Manual is refused by every preview '
            'before 0.13.20. In the Audio Unit and LV2, previews before 0.13.20 have no Gain mode, so they play such '
            'a project at its Gain setting, which can be louder. Projects and presets saved with earlier previews '
            'open in this one.'
        ),
        '## Report a problem',
        (
            "Copy Diagnostics from Convolver's window on macOS and Windows, or attach the Convolver log on Linux "
            f'([where it is]({docs}/report-a-problem.md#2-collect-convolvers-messages)). '
            'Say which system, host and version you used, how you loaded the filter '
            "(on macOS and Windows the Choose Filter menu's Choose File…, a recent file or Reload; in LV2 the "
            "host's file chooser; anywhere a preset or a "
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


def production_policy(policy, requirements):
    """The tag's release policy, judged by the production requirements of
    `requirements` (this checkout's policy): its production_enabled, its
    required evidence and each platform's production rules."""
    rules = {platform['id']: platform.get('production') for platform in requirements['platforms']}
    platforms = []
    for original in policy['platforms']:
        platform = {key: value for key, value in original.items() if key != 'production'}
        # A platform without rules stays without them: the gate lists it.
        if rules.get(platform['id']) is not None:
            platform['production'] = rules[platform['id']]
        platforms.append(platform)
    return dict(
        policy,
        production_enabled=requirements.get('production_enabled', False),
        required_production_evidence=requirements['required_production_evidence'],
        platforms=platforms,
    )


def prepare(
    run,
    root,
    tagged,
    tag,
    repository,
    qualification,
    output,
    platforms=PUBLISHED_PLATFORMS,
    *,
    production=False,
    requirements=REQUIREMENTS,
    evidence=None,
    dry_run=False,
):
    """Checks the candidate and writes the notes, SHA256SUMS and title to
    `output`. Returns the names of the files to attach and, for a production
    dry run, the production gates the candidate does not meet (otherwise any
    unmet gate refuses)."""
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
    problems = []
    if production:
        policy = production_policy(policy, load_policy(requirements))
        if evidence is not None:
            candidate = with_evidence(candidate, evidence, policy)
        problems = production_problems(candidate, policy)
        if problems and not dry_run:
            raise ValueError('not ready for a production release: ' + '; '.join(problems))
    files = select(candidate, platforms)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'notes.md').write_text(
        notes(
            version,
            tag,
            repository,
            run,
            commit,
            files,
            sums,
            qualification.strip(),
            policy if production else None,
        ),
        encoding='utf-8',
    )
    (output / 'SHA256SUMS').write_text(''.join(f'{sums[name]}  {name}\n' for name, _, _ in files), encoding='utf-8')
    title = f'Convolver {version}' if production else f'Convolver {version} (unsigned developer preview)'
    (output / 'title.txt').write_text(title + '\n', encoding='utf-8')
    return [name for name, _, _ in files], problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--run', type=Path, required=True, help="the run's description, as JSON from the API")
    parser.add_argument('--root', type=Path, required=True, help="the run's complete-preview-candidate files")
    parser.add_argument('--tagged', type=Path, required=True, help='a checkout of the tag')
    parser.add_argument('--tag', required=True)
    parser.add_argument('--repository', required=True, help='OWNER/NAME')
    parser.add_argument('--qualification', required=True, help='the REL-6 result, for the release notes')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--production', action='store_true', help='a full release, not a pre-release (REL-1)')
    parser.add_argument(
        '--evidence',
        type=Path,
        help='with --production: the release evidence file; one that does not exist counts as no evidence',
    )
    parser.add_argument(
        '--dry-run', action='store_true', help='with --production: list the unmet production gates instead of refusing'
    )
    args = parser.parse_args()
    if (args.evidence or args.dry_run) and not args.production:
        parser.error('--evidence and --dry-run belong to --production')
    run = json.loads(args.run.read_text(encoding='utf-8'))
    evidence = None
    if args.evidence and args.evidence.is_file():
        evidence = json.loads(args.evidence.read_text(encoding='utf-8'))
    names, problems = prepare(
        run,
        args.root,
        args.tagged,
        args.tag,
        args.repository,
        args.qualification,
        args.output,
        production=args.production,
        evidence=evidence,
        dry_run=args.dry_run,
    )
    print(f'{args.tag}: run {run["id"]} checked; files to attach:')
    for name in names:
        print('  ' + name)
    if args.production and evidence is None:
        print(f'No release evidence file at {args.evidence}.' if args.evidence else 'No release evidence file given.')
    for problem in problems:
        print(f'::warning::Not ready for a production release: {problem}')
    if args.production and not problems:
        print('Every production gate passed.')


if __name__ == '__main__':
    main()
