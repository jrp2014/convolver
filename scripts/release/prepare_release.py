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
PREVIOUS_PREVIEW = '0.13.2'
CHANGES = (
    (
        "On Windows the VST3 plug-in has Convolver's window, as on macOS: choose, drop or reload a filter, set "
        'Gain, Mix and Latency, and read the status and Copy Diagnostics there. The Windows and Linux plug-in '
        'reads filter files with libsndfile, as the command-line tool does.'
    ),
    (
        'WAV files that other programs write and Convolver refused as cut short are now read: a last chunk '
        "without its padding byte, as Python's `wave` module writes mono 24-bit audio with an odd number of "
        'frames, and sizes that were never filled in, as FFmpeg writes to a pipe. Such a file of 4 GiB or more '
        'is still refused.'
    ),
    (
        'A sound file that cannot be used as a filter is reported with the reason, for example a damaged header '
        'or too many channels, instead of a general message. `convolver --inspect` gives such a file the new '
        'code `unreadable-asset`.'
    ),
    (
        'Linux packages include the LV2 plug-in, one plug-in for each channel layout, with its filter chosen with '
        "the host's file chooser or an LV2 preset made with `convolver-preset`."
    ),
    (
        "In Audacity, a filter loaded from Convolver's window (Choose…, Recent or Reload) is now kept for exports "
        'and saved projects; checked by automated tests only, not yet in Audacity.'
    ),
    (
        'On Windows, a file whose name holds characters outside the system code page, such as Greek, Cyrillic '
        'or Chinese letters, is named correctly in messages, and a configuration naming such an IR file loads.'
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
            '- The LV2 plug-in (Linux) is new in the previews. It has no window and no Gain, Mix or Latency yet: '
            "choose a filter with the host's file chooser for its Filter parameter, where the host shows one, or "
            f'load an LV2 preset made with `convolver-preset` ([plug-in presets]({docs}/presets.md)). Each channel '
            'layout is '
            'its own plug-in. It was checked only automatically, with lilv, lv2lint, pluginval and a headless '
            'Ardour session.\n'
            '- The Windows window is new and has been checked only automatically, on Windows CI machines: it opens, '
            "and the plug-in validator's window tests pass. Nobody has tried it in a Windows host or with a screen "
            'reader yet.\n'
            '- Unsigned: macOS asks you to confirm that you want to open it, and Windows shows a SmartScreen '
            'warning.\n'
            '- Windows needs the Microsoft Visual C++ 2015-2022 x64 runtime, which most computers already have.\n'
            '- An early preview, published before its last checks. Its new audio engine, which adds no delay at '
            'Latency 0, has been checked only automatically, against an exact reference and the command-line tool, '
            'on macOS, Windows and Linux; nobody has listened to it in a host yet. With the previous engine '
            '(0.12.12), on one Mac, renders and exports from REAPER and Audacity (VST3) and GarageBand (Audio Unit) '
            'matched the command-line tool, and saved projects reopened with their filter. Not yet checked: '
            'listening in any host, VoiceOver, the latest Audio Unit changes in GarageBand, and anyone outside the '
            'project following the guides. The Windows and Linux packages were '
            'checked only automatically, on CI machines that had never built Convolver: the tools and examples ran '
            "from them, the plug-in's library loaded and listed its classes, and the Windows installer installed "
            'and removed itself. Nobody has used them in a host yet. '
            f'[Status]({base}/blob/{tag}/docs/project/status.md#checks-left-before-publication) lists what is open. '
            'Other hosts are untested.\n'
            "- In Audacity, Convolver's window (Choose…, Recent or Reload) should now keep the filter for exports and "
            "saved projects, but this has been checked only by automated tests, not yet in Audacity. Audacity's own "
            'preset menu is the route that has been checked.\n'
            '- Since 0.13, Latency starts at 0 frames. Some projects saved with 0.12.12 at its default of 256 frames '
            'open at 0; choose 256 again if you want its delay back '
            f'([projects from earlier versions]({docs}/plugins.md#settings)).'
        ),
        '## Report a problem',
        (
            "Copy Diagnostics from Convolver's window on macOS and Windows, or attach the Convolver log on Linux "
            f'([where it is]({docs}/report-a-problem.md#2-collect-convolvers-messages)). '
            'Say which system, host and version you used, how you loaded the filter '
            '(on macOS and Windows Choose…, Recent or Reload; anywhere a preset or a saved project), '
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
