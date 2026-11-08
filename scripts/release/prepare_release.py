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

# The first preview is macOS only (docs/project/decisions.md, 6 November
# 2026); the run also builds Windows and Linux, which are not attached.
PUBLISHED_PLATFORMS = ('macos-arm64', 'macos-x86_64')
AUDIENCE = 'macOS 14 or later, on Apple silicon or Intel'
CANDIDATE_WORKFLOW = '.github/workflows/release.yml'
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
            f'**Unsigned developer preview.** Convolver {version} for {AUDIENCE}: the VST® 3 plug-in, the Audio Unit, '
            'the `convolver` command-line tool, `convolver-preset` and `convolver-equalizer-apo`, with example presets '
            'and the user guides. It is not signed with an Apple Developer ID: read the '
            f'[install guide]({docs}/install.md#unsigned-previews) before opening it.'
        ),
        '## Download',
        (
            'Choose the installer (`.pkg`) or the ZIP archive for your Mac, and check its SHA-256 before opening it '
            '(`shasum -a 256 <file>` in Terminal).'
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
        '## Known limitations',
        (
            '- macOS only. Windows, Linux and the LV2 plug-in come with later previews; until then, build from '
            'source.\n'
            '- Unsigned: macOS asks you to confirm that you want to open it.\n'
            '- An early preview, published before its last checks. On one Mac, renders and exports from REAPER and '
            'Audacity (VST3) and GarageBand (Audio Unit) matched the command-line tool, and saved projects reopened '
            'with their filter. Not yet checked: listening in any host, VoiceOver, the latest Audio Unit change in '
            'GarageBand, and anyone outside the project following the guides. '
            f'[Status]({base}/blob/{tag}/docs/project/status.md#checks-left-before-publication) lists what is open. '
            'Other hosts are untested.\n'
            "- In Audacity, load filters from Audacity's own preset menu. A filter loaded from Convolver's window "
            '(Choose…, Recent or Reload) plays there, but Audacity 4 does not keep it for exports or saved projects.'
        ),
        '## Report a problem',
        (
            "Copy Diagnostics from Convolver's window, and say which host and version you used, how you loaded the "
            'filter (Choose…, Recent, Reload, a preset or a saved project), and whether the problem is in playback, '
            f'export or reopening ([how to report]({docs}/troubleshooting.md#report-a-problem)).'
        ),
        '## Loading responsiveness',
        qualification,
        '## How it was built',
        (
            f'Built and tested by [run {run["id"]}]({run["html_url"]}) from commit `{commit}` (tag `{tag}`). The same '
            'run built Windows and Linux packages, which are not part of this preview. `Convolver-source.tar.gz` is '
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
