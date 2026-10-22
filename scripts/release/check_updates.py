# SPDX-License-Identifier: GPL-2.0-or-later
"""Look for newer upstream releases of the pinned SDKs and test tools.

Reads each pin from where it is defined (the release policy or a workflow),
lists the upstream tags with `git ls-remote` (no API token or rate limit) and
reports, as Markdown, which pins have a newer release. It also checks that a
policy pin's tag still points at the pinned commit and that the VST3 workflow
checks out the same SDK commit as the policy.

python3 scripts/release/check_updates.py [--markdown FILE] [--github-output FILE]

It changes nothing: moving a pin is a reviewed pull request (see "Pinned format
dependencies" in docs/plugins/build.md). GitHub Actions versions are kept
current by Dependabot, so they are not listed here.
"""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
POLICY = ROOT / 'packaging/release-policy.json'
PRE_RELEASE = re.compile(r'(?P<number>\d+(?:\.\d+)*)(?:[._-]build[._-](?P<build>\d+))?'
                         r'(?:[._-]?(?P<pre>alpha|beta|rc|pre)(?P<pre_number>\d*))?$')


@dataclass(frozen=True)
class Pin:
    name: str
    remote: str
    tag_prefix: str
    where: str
    version: str = ''
    revision: str = ''
    note: str = ''
    actionable: bool = True


def version_key(text):
    """Orders versions: numbers, then build, then final above pre-releases."""
    match = PRE_RELEASE.fullmatch(text)
    if not match:
        return None
    numbers = tuple(int(part) for part in match['number'].split('.'))
    numbers += (0,) * (4 - len(numbers))
    build = int(match['build'] or 0)
    final = match['pre'] is None
    pre_number = int(match['pre_number'] or 0)
    return numbers, build, final, pre_number


def is_final(text):
    key = version_key(text)
    return key is not None and key[2]


def parse_ls_remote(output):
    """Maps tag name to commit, preferring the peeled commit of an annotated tag."""
    tags = {}
    for line in output.splitlines():
        if '\t' not in line:
            continue
        commit, ref = line.split('\t', 1)
        if not ref.startswith('refs/tags/'):
            continue
        name = ref[len('refs/tags/'):]
        if name.endswith('^{}'):
            tags[name[:-3]] = commit
        else:
            tags.setdefault(name, commit)
    return tags


def ls_remote(remote):
    result = subprocess.run(['git', 'ls-remote', '--tags', remote], capture_output=True, text=True,
                            timeout=120, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr.strip().splitlines()[-1] if result.stderr.strip() else
                           f'git ls-remote exited with {result.returncode}')
    return result.stdout


def tag_versions(tags, prefix):
    """Yields (version, tag) for the tags that name a release of this pin."""
    for tag in tags:
        if tag.startswith(prefix):
            version = tag[len(prefix):]
            if version_key(version) is not None:
                yield version, tag


def _workflow_value(text, name, path):
    match = re.search(rf'^\s*{re.escape(name)}:\s*(\S+)', text, re.MULTILINE)
    if not match:
        raise ValueError(f'{path}: no {name} pin found')
    return match.group(1).strip('\'"')


def pins(root=ROOT):
    """Every pin this checker follows, read from where it is defined."""
    policy = json.loads((root / 'packaging/release-policy.json').read_text(encoding='utf-8'))
    deps = policy['dependencies']
    vst3 = (root / '.github/workflows/vst3.yml').read_text(encoding='utf-8')
    lv2 = (root / '.github/workflows/lv2.yml').read_text(encoding='utf-8')
    sdk = deps['vst3sdk']
    vst3_version = re.sub(r'-build-', '_build_', sdk['version'])
    result = [
        Pin('VST3 SDK (with its four submodules)', 'https://github.com/steinbergmedia/vst3sdk', 'v',
            'release policy, VST3 workflow', vst3_version, sdk['revision']),
        Pin('lv2 headers', 'https://github.com/lv2/lv2', 'v', 'release policy',
            deps['lv2']['version'], deps['lv2']['revision']),
        Pin('AudioUnitSDK', 'https://github.com/apple/AudioUnitSDK', 'AudioUnitSDK-', 'release policy',
            deps['audiounitsdk']['version'], deps['audiounitsdk']['revision']),
        Pin('FFTW', 'https://github.com/FFTW/fftw3', 'fftw-', 'release policy', deps['fftw']['version']),
        Pin('libsndfile', 'https://github.com/libsndfile/libsndfile', '', 'release policy',
            deps['libsndfile']['version']),
        Pin('pluginval', 'https://github.com/Tracktion/pluginval', 'v', 'VST3 workflow',
            _workflow_value(vst3, 'PLUGINVAL_TAG', 'vst3.yml').removeprefix('v')),
        Pin('lv2lint', 'https://git.open-music-kontrollers.ch/~hp/lv2lint', '', 'LV2 workflow',
            _workflow_value(lv2, 'LV2LINT_VERSION', 'lv2.yml')),
        Pin('lilv', 'https://github.com/lv2/lilv', 'v', 'LV2 workflow (Ubuntu 24.04 package)',
            _workflow_value(lv2, 'LILV_DEB', 'lv2.yml').split('-')[0],
            note='Follows the ubuntu-24.04 image; move it with the runner image, not on its own.',
            actionable=False),
    ]
    checkout = re.search(r'repository:\s*steinbergmedia/vst3sdk\s*\n\s*ref:\s*([0-9a-f]{40})', vst3)
    if not checkout:
        raise ValueError('vst3.yml: no pinned steinbergmedia/vst3sdk checkout found')
    return result, checkout.group(1)


@dataclass
class Row:
    pin: Pin
    newest: str = ''
    status: str = ''
    update: bool = False
    problem: bool = False


def check(pin, tags):
    """Compares one pin with its upstream tags."""
    row = Row(pin)
    releases = dict(tag_versions(tags, pin.tag_prefix))
    current = version_key(pin.version)
    if current is None:
        row.status, row.problem = f'cannot read the pinned version {pin.version!r}', True
        return row
    # Pre-releases count only when the pin is itself a pre-release.
    candidates = [v for v in releases if is_final(v) or not current[2]]
    if not candidates:
        row.status, row.problem = 'no release tags found upstream', True
        return row
    newest = max(candidates, key=version_key)
    row.newest = newest
    if version_key(newest) > current:
        row.update = pin.actionable
        row.status = 'newer release' if pin.actionable else 'newer upstream (for information)'
    else:
        row.status = 'current'
    if pin.revision:
        tag = pin.tag_prefix + pin.version
        if tag not in tags:
            row.status, row.problem = f'pinned tag {tag} is missing upstream', True
        elif tags[tag] != pin.revision:
            row.status, row.problem = (f'tag {tag} now points at {tags[tag][:12]}, '
                                       f'not the pinned {pin.revision[:12]}'), True
    return row


def run(lister=ls_remote, root=ROOT):
    pin_list, checkout = pins(root)
    rows = []
    for pin in pin_list:
        try:
            tags = parse_ls_remote(lister(pin.remote))
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            rows.append(Row(pin, status=f'lookup failed: {error}', problem=True))
            continue
        rows.append(check(pin, tags))
    if checkout != pin_list[0].revision:
        rows.append(Row(Pin('VST3 workflow checkout', '', '', 'VST3 workflow', revision=checkout),
                        status=f'checks out {checkout[:12]}, but the policy pins {pin_list[0].revision[:12]}',
                        problem=True))
    return rows


def markdown(rows):
    updates = [row for row in rows if row.update]
    problems = [row for row in rows if row.problem]
    if updates or problems:
        lines = [f'{len(updates)} pin(s) have a newer release and {len(problems)} need attention.']
    else:
        lines = ['Every pin is on its newest release.']
    lines += [
        '',
        '| Dependency | Pinned | Newest upstream | Where pinned | Status |',
        '| --- | --- | --- | --- | --- |',
    ]
    for row in rows:
        status = f'**{row.status}**' if row.update or row.problem else row.status
        lines.append(f'| {row.pin.name} | {row.pin.version or "-"} | {row.newest or "-"} | '
                     f'{row.pin.where} | {status} |')
    notes = [f'- {row.pin.name}: {row.pin.note}' for row in rows if row.pin.note]
    if notes:
        lines += ['', *notes]
    lines += [
        '',
        'Move a pin in its own pull request, with the reason and the new checksum; see '
        '"Pinned format dependencies" in `docs/plugins/build.md`. GitHub Actions versions '
        'are updated by Dependabot.',
    ]
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--markdown', type=Path, help='also write the report to this file')
    parser.add_argument('--github-output', type=Path,
                        help='append attention=true|false to this GitHub Actions output file')
    args = parser.parse_args(argv)
    rows = run()
    report = markdown(rows)
    sys.stdout.write(report)
    if args.markdown:
        args.markdown.write_text(report, encoding='utf-8')
    attention = any(row.update or row.problem for row in rows)
    if args.github_output:
        with args.github_output.open('a', encoding='utf-8') as output:
            output.write(f'attention={"true" if attention else "false"}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
