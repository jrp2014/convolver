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
from promotion_gate import (
    check,
    evidence_holds,
    host_covered,
    host_tests,
    passed,
    platform_files,
    production_problems,
    with_evidence,
)

# Every platform the candidate run builds: the first preview was macOS only,
# and Windows and Linux joined from the next one (REL-9, jrp's decision of
# 8 November 2026 in docs/project/decisions.md).
PUBLISHED_PLATFORMS = ('macos-arm64', 'macos-x86_64', 'windows-x86_64', 'linux-x86_64')
AUDIENCE = 'macOS 14 or later, Windows 11 and Linux (x86-64, glibc 2.35 or later)'
CANDIDATE_WORKFLOW = '.github/workflows/release.yml'
TAG = re.compile(r'v(\d+\.\d+\.\d+)')
REQUIREMENTS = Path(__file__).resolve().parents[2] / 'packaging/release-policy.json'
# The words that change from one release to the next (REL-13, jrp's review of
# 7bd024f): the preview published before, what changed since and the known
# limitations, kept out of this script so that a wording change is not a
# change to the release gate. Each preview's release-notes pull request
# updates it (docs/develop/releasing.md#publishing-an-unsigned-preview).
NOTES_TEXT = Path(__file__).resolve().parents[2] / 'packaging/release-notes.md'
PLACEHOLDERS = {'docs', 'base', 'tag'}
AUDIENCES = {'previews': 'preview', 'full releases': 'production', 'generated': 'generated'}
SYSTEMS = {'Darwin': 'macOS', 'Windows': 'Windows', 'Linux': 'Linux'}
# A plug-in format as the notes name it (REL-17).
FORMATS = {'vst3': 'VST3', 'au': 'the Audio Unit', 'lv2': 'LV2'}
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


def verification_facts(policy, candidate):
    """What the evidence attached to the candidate shows, for each platform
    of a full release and each of its files (REL-12, jrp's reviews of 7bd024f
    and 547e2dd). Each file gives its name, what it is ('ZIP archive',
    'installer', or with several installers 'installer `name`'), the kinds
    of evidence that hold for it, its host tests that hold (each with its
    plug-in format, host, version, system build and record; REL-17: one per
    format the platform ships), and what is still outstanding that its
    platform's `unverified` rules let it ship without: a kind, or for host
    tests 'host:FORMAT' for each format without one. The gate judges each
    file on its own evidence, so one file of a platform can hold a host test
    that another lacks. A kind the rules require and the evidence lacks is
    the gate's to refuse, not the notes' to mention."""
    assets = {asset['name']: asset for asset in candidate['assets']}
    records = {record['id']: record for record in candidate['platforms']}
    kinds = policy['required_production_evidence']
    facts = []
    for platform in policy['platforms']:
        record = records.get(platform['id'])
        rules = platform.get('production', {'signed': False, 'unverified': []})
        files = []
        found = platform_files(record, assets) if record else []
        for index, (name, evidence, digest) in enumerate(found):
            verified = [kind for kind in kinds if evidence_holds(kind, evidence, platform, digest)]
            tests = host_tests(evidence.get('evidence', {}))
            held = [
                f for f in platform['formats'] if passed(tests.get(f, {}), digest) and host_covered(tests[f], platform)
            ]
            hosts = [
                dict({key: tests[f][key] for key in ('host', 'host_version', 'os_build', 'record')}, format=f)
                for f in held
            ]
            outstanding = []
            for kind in rules['unverified']:
                if kind == 'host':
                    outstanding += [f'host:{f}' for f in platform['formats'] if f not in held]
                elif kind not in verified:
                    outstanding.append(kind)
            files.append(
                {
                    'name': name,
                    'label': 'ZIP archive' if index == 0 else 'installer' if len(found) == 2 else f'installer `{name}`',
                    'verified': verified,
                    'hosts': hosts,
                    'outstanding': outstanding,
                }
            )
        facts.append(
            {
                'platform': platform['id'],
                'system': SYSTEMS[platform['os']],
                'architecture': platform['architecture'],
                'formats': platform['formats'],
                'files': files,
            }
        )
    return facts


def lacking(facts, kind):
    """Where `kind` is outstanding, named for the notes: a system alone when
    every file of all its platforms lacks it, a system and architecture when
    every file of that platform does, otherwise the files that lack it."""
    names = []
    for fact in facts:
        missing = [f for f in fact['files'] if kind in f['outstanding']]
        if not missing:
            continue
        system = [other for other in facts if other['system'] == fact['system']]
        if all(kind in f['outstanding'] for other in system for f in other['files']):
            names.append(fact['system'])  # Once: systems() drops repeats.
        elif len(missing) == len(fact['files']):
            names.append(f'{fact["system"]} ({fact["architecture"]})')
        else:
            labels = systems(dict.fromkeys(f['label'] for f in missing))
            names.append(f'the {fact["system"]} ({fact["architecture"]}) {labels}')
    return names


def production_lines(policy, candidate, docs, base, tag):
    """The opening paragraph's signing and host-test sentences and the
    generated known limitations of a full release: the signing from each
    platform's production rules, what was tested from verification_facts()."""
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
    facts = verification_facts(policy, candidate)
    tested = []
    for fact in facts:
        # Each distinct host test, with the files it holds for in each
        # format; formats are named together only when they hold for the
        # same files, and the files only when not all of them.
        tests = {}
        for file in fact['files']:
            for test in file['hosts']:
                key = tuple((k, v) for k, v in test.items() if k != 'format')
                labels = tests.setdefault(key, {}).setdefault(test['format'], [])
                labels.append(file['label'])
        for test, formats in tests.items():
            host = dict(test)
            groups = {}
            for f, labels in formats.items():
                groups.setdefault(tuple(dict.fromkeys(labels)), []).append(f)
            for labels, same in groups.items():
                which = '' if len(labels) == len(fact['files']) else f', with the {systems(labels)}'
                tested.append(
                    f'[{host["host"]} {host["host_version"]}]({host["record"]}) on {fact["system"]} '
                    f'({host["os_build"]}, {fact["architecture"]}) for {systems(FORMATS[f] for f in same)}'
                    f'{which}'
                )
    if tested:
        sentence.append(
            'Tested by hand in a host with the files below, at 44.1, 48 and 96 kHz on mono and stereo tracks: '
            + '; '.join(tested)
            + '.'
        )
    status = f'[status]({base}/blob/{tag}/docs/project/status.md)'
    # REL-17: what lacks a host test, by plug-in format, with the formats
    # each system (or architecture, or file) lacks named together.
    places = {}
    for f in dict.fromkeys(f for fact in facts for f in fact['formats']):
        for where in lacking(facts, f'host:{f}'):
            places.setdefault(where, []).append(FORMATS[f])
    hostless = ', or for '.join(f'{systems(formats)} on {where}' for where, formats in places.items())
    if hostless:
        files = [f for fact in facts for f in fact['files'] if any(k.startswith('host:') for k in f['outstanding'])]
        # Said only when the evidence shows it for every one of those files.
        automatic = all({'automated', 'clean_install'} <= set(f['verified']) for f in files)
        checked = ', and each was checked only automatically, on CI machines that had never built Convolver'
        limitations.append(
            f'- Not verified in a named host: no host test was accepted for {hostless} in this release'
            f'{checked if automatic else ""} ({status}).'
        )
    others = dict.fromkeys(
        k for fact in facts for f in fact['files'] for k in f['outstanding'] if not k.startswith('host:')
    )
    limitations.extend(f'- Not verified on {systems(lacking(facts, k))}: {k.replace("_", " ")}.' for k in others)
    return ' '.join(sentence), limitations


def notes_text(path=NOTES_TEXT):
    """The release notes' words from `path`: {'previous': the preview
    published before, 'changes': what changed, 'limitations': (audience,
    text) pairs, audience 'both', 'preview', 'production' or 'generated'}.
    Each list entry's lines are joined into one, as GitHub shows every line
    break. Raises ValueError, naming the file and the problem, for anything
    else."""
    sections, current = {}, None
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        if line.startswith('## '):
            current = line[3:].strip()
            if current in sections:
                raise ValueError(f'{path.name}:{number}: a second "## {current}" section')
            sections[current] = []
        elif current is not None:
            sections[current].append((number, line))

    def problem(words, number=None):
        return ValueError(f'{path.name}{f":{number}" if number else ""}: {words}')

    def entries(name):
        if name not in sections:
            raise problem(f'no "## {name}" section')
        items = []
        for number, line in sections[name]:
            if line.startswith('- '):
                items.append(line[2:].strip())
            elif line.startswith('  ') and line.strip() and items:
                items[-1] += ' ' + line.strip()
            elif line.strip():
                raise problem(f'"## {name}" holds only list entries, each line after the first indented', number)
        if not items:
            raise problem(f'"## {name}" is empty')
        for item in items:
            # A comment GitHub hides, or a placeholder left as written, would
            # publish words nobody reviewed in the notes.
            words = re.sub(r'^<!-- (.+?) -->', '', item) if name == 'Known limitations' else item
            if '<!--' in words or '-->' in words:
                raise problem(f'a comment in "## {name}" other than a leading marker: {item[:60]!r}')
            unknown = set(re.findall(r'\{([^}]*)\}', words)) - PLACEHOLDERS
            if unknown:
                raise problem(f'unknown placeholder {{{min(unknown)}}} in "## {name}"')
        return items

    previous = ' '.join(line.strip() for _, line in sections.get('Previous preview', []) if line.strip())
    if not TAG.fullmatch('v' + previous):
        raise problem(f'"## Previous preview" must hold one version, X.Y.Z, not {previous!r}')
    limitations = []
    for item in entries('Known limitations'):
        marker = re.match(r'<!-- (.+?) -->\s*', item)
        audience = AUDIENCES.get(marker.group(1)) if marker else 'both'
        if audience is None:
            raise problem(f'unknown marker {marker.group(0).strip()!r} in "## Known limitations"')
        text = item[marker.end() :] if marker else item
        if (audience == 'generated') != (not text):
            raise problem('"<!-- generated -->" stands alone, and every other limitation has words')
        limitations.append((audience, text))
    if [audience for audience, _ in limitations].count('generated') != 1:
        raise problem('"## Known limitations" needs exactly one "- <!-- generated -->" entry')
    return {'previous': previous, 'changes': entries('What changed'), 'limitations': limitations}


def notes(version, tag, repository, run, commit, files, sums, qualification, production=None, candidate=None):
    """The release notes; `production` is the release policy for a full
    release, None for a preview, and `candidate` then the candidate with its
    evidence."""
    base = f'https://github.com/{repository}'
    docs = f'{base}/blob/{tag}/docs/use'
    generated = []
    if production:
        signing, generated = production_lines(production, candidate, docs, base, tag)
    text = notes_text()

    def filled(words):
        return words.replace('{docs}', docs).replace('{base}', base).replace('{tag}', tag)

    audience = 'production' if production else 'preview'
    limitations = []
    for kind, words in text['limitations']:
        if kind == 'generated':
            limitations.extend(generated)
        elif kind in ('both', audience):
            limitations.append(f'- {filled(words)}')
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
        f'Since {text["previous"]}:',
        '\n'.join(f'- {filled(change)}' for change in text['changes']),
        # REL-21: a full release's notes do not call it a preview.
        (
            f"Each {'release' if production else 'preview'}'s changes are in the "
            f'[changelog]({base}/blob/{tag}/CHANGELOG.md).'
        ),
        '## Known limitations',
        '\n'.join(limitations),
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
            'To say what worked, what was hard to follow or what you missed, open '
            + (
                f'an [issue]({base}/issues/new/choose).'
                if production
                else f'a [Preview feedback]({base}/issues/new?template=preview_feedback.yml) issue.'
            )
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
            candidate,
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
