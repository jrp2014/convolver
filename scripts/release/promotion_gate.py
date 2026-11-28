# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate exact candidate assets and reviewed evidence before promotion.

`check` is the developer-preview integrity gate: every platform's files, its
corresponding source and notices, and unchanged hashes. With `production`, it
also requires every production gate (`production_problems`): what each
platform's `production` rules in the release policy ask for (REL-1), with
evidence bound to the final hashes. `with_evidence` adds the reviewed evidence
of a release evidence file (packaging/evidence/X.Y.Z.json) to a candidate.
See docs/develop/releasing.md#production-gates.
"""

import argparse
import copy
import json
import re
from pathlib import Path

from contract import load_policy, sha256


def text(value):
    """Whether evidence gives a non-empty string here (REL-20): a list or a
    number where a name belongs is refused, never passed on to the notes."""
    return isinstance(value, str) and bool(value.strip())


# What the release notes name for each accepted host test (REL-20).
HOST_FIELDS = ('host', 'host_version', 'os_build')


def passed(entry, digest):
    """Whether one piece of evidence passed, on these bytes, with a record to read."""
    record = entry.get('record')
    return (
        entry.get('result') == 'passed'
        and entry.get('artifact_sha256') == digest
        and text(record)
        and record.startswith('https://')
    )


REQUIRED_SAMPLE_RATES = {44100, 48000, 96000}
REQUIRED_CHANNELS = {1, 2}


def _whole_numbers(value):
    """The whole numbers a list holds; nothing for anything else, so evidence
    of the wrong shape is refused rather than failing the gate."""
    if not isinstance(value, list):
        return set()
    return {item for item in value if isinstance(item, int) and not isinstance(item, bool)}


def host_covered(host, platform):
    """Whether host evidence names its host, version and system build, each
    as text, and covers the platform's architecture, 44.1, 48 and 96 kHz,
    mono and stereo. The release notes name only tests this accepts, so the
    gate and the notes read the same validated record (REL-20).
    REL-15 (jrp's first-pass review of 0.18.27 to 0.18.34): evidence that
    also covers other rates or layouts (192 kHz, a 6-channel crossover)
    covers these too."""
    return (
        all(text(host.get(key)) for key in HOST_FIELDS)
        and host.get('architecture') == platform['architecture']
        and _whole_numbers(host.get('sample_rates')) >= REQUIRED_SAMPLE_RATES
        and _whole_numbers(host.get('channels')) >= REQUIRED_CHANNELS
    )


def host_tests(evidence):
    """A ZIP's or installer's host tests by plug-in format (REL-17): each
    format the file ships ("vst3", "au", "lv2") is tested on its own, so a
    macOS file needs a VST3 test and an Audio Unit test. Nothing for
    evidence of any other shape."""
    tests = evidence.get('host', {})
    return tests if isinstance(tests, dict) else {}


def host_formats(platform, rules, tests):
    """The formats whose host tests the gate checks: every format the
    platform ships when its rules require host tests; where they may ship
    without (`unverified`), only those given, since a failed test is never
    ignored."""
    if 'host' in rules['unverified']:
        return [f for f in platform['formats'] if f in tests]
    return list(platform['formats'])


def evidence_holds(kind, record, platform, digest):
    """Whether a ZIP's or installer's evidence of `kind` is what the gate
    accepts: passed, on these bytes, recorded, and for host tests one for
    every format the platform ships, each with the coverage it needs. The
    release notes say only what holds (REL-12)."""
    evidence = record.get('evidence', {})
    if kind != 'host':
        return passed(evidence.get(kind, {}), digest)
    tests = host_tests(evidence)
    return all(
        f in tests and passed(tests[f], digest) and host_covered(tests[f], platform) for f in platform['formats']
    )


def platform_files(record, assets):
    """A candidate platform's ZIP and installers, each with its own
    qualification record and SHA-256."""
    files = [(record['artifact'], record, assets[record['artifact']]['sha256'])]
    for installer in record.get('installers', []):
        if installer in assets:
            qualification = record.get('installer_evidence', {}).get(installer, {})
            files.append((installer, qualification, assets[installer]['sha256']))
    return files


def qualification_problems(label, record, platform, policy, digest):
    """What one ZIP or installer lacks for production, under its platform's rules."""
    rules = platform.get('production')
    if rules is None:
        return [f'{label}: the release policy gives no production rules for {platform["id"]}']
    problems = []
    if rules['signed'] and record.get('signed') is not True:
        problems.append(f'{label}: not signed')
    if rules['signed'] and platform['os'] == 'Darwin' and record.get('notarized') is not True:
        problems.append(f'{label}: not notarized')
    evidence = record.get('evidence', {})
    tests = host_tests(evidence)
    formats = host_formats(platform, rules, tests)
    # An unverified kind is not required, but a result given for it must
    # still have passed, on these bytes: a failed test is never ignored.
    required = [
        kind for kind in policy['required_production_evidence'] if kind not in rules['unverified'] or kind in evidence
    ]
    missing = []
    for kind in required:
        if kind != 'host':
            if not passed(evidence.get(kind, {}), digest):
                missing.append(kind)
            continue
        # REL-17 (pre-1.0 review of 0.19.21): one host test per format, so
        # a REAPER VST3 run no longer stands for an untested Audio Unit.
        absent = [f for f in formats if not passed(tests.get(f, {}), digest)]
        if absent or not formats:
            missing.append(f'host ({", ".join(absent or platform["formats"])})')
    if missing:
        problems.append(f'{label}: missing, failed or stale production evidence: {", ".join(missing)}')
    if 'host' in required and not any(m.startswith('host') for m in missing):
        problems.extend(
            f'{label}: {f} host evidence lacks required platform/rate/layout coverage'
            for f in formats
            if not host_covered(tests[f], platform)
        )
    return problems


def production_problems(candidate, policy):
    """Every production gate that a candidate which passed `check` does not meet."""
    problems = []
    # REL-15: only true enables it; contract.validate_policy refuses any
    # other value, and this refuses one that reached it anyway.
    if policy.get('production_enabled') is not True:
        problems.append('production publication is disabled pending release qualification (production_enabled)')
    assets = {asset['name']: asset for asset in candidate['assets']}
    expected = {p['id']: p for p in policy['platforms']}
    qualified_installers = set()
    for record in candidate['platforms']:
        platform = expected[record['id']]
        name = record['artifact']
        problems += qualification_problems(name, record, platform, policy, assets[name]['sha256'])
        installers = record.get('installers', [])
        if platform['os'] in ('Windows', 'Darwin') and not installers:
            problems.append(f'{record["id"]}: missing production installer')
        for installer in installers:
            if installer in qualified_installers or assets.get(installer, {}).get('kind') != 'installer':
                problems.append(f'{installer}: invalid/duplicate installer association')
                continue
            problems += qualification_problems(
                installer,
                record.get('installer_evidence', {}).get(installer, {}),
                platform,
                policy,
                assets[installer]['sha256'],
            )
            qualified_installers.add(installer)
        # A full release's files must not call themselves a preview, nor
        # unsigned where its platform is signed.
        for file in [name, *installers]:
            if '-preview' in file:
                problems.append(f'{file}: the file name says preview')
            if platform.get('production', {}).get('signed') and '-unsigned' in file:
                problems.append(f'{file}: the file name says unsigned')
    unqualified = {name for name, asset in assets.items() if asset['kind'] == 'installer'} - qualified_installers
    if unqualified:
        problems.append(f'unqualified installer asset: {", ".join(sorted(unqualified))}')
    return problems


def check(candidate, policy, root, *, production=False):
    if candidate.get('schema') != 1 or candidate.get('version') != policy['version']:
        raise ValueError('candidate version/schema mismatch')
    commit = candidate.get('source_commit', '')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('candidate requires full immutable source commit')
    assets = {}
    for asset in candidate.get('assets', []):
        name = asset.get('name', '')
        if not name or Path(name).name != name or '\\' in name or name in assets:
            raise ValueError('invalid/duplicate candidate asset name')
        path = root / name
        if path.is_symlink() or not path.is_file() or sha256(path) != asset.get('sha256'):
            raise ValueError('missing asset or changed asset hash: ' + name)
        if asset.get('kind') not in ('binary', 'source', 'manifest', 'installer'):
            raise ValueError('unclassified candidate asset')
        assets[name] = asset
    expected = {p['id']: p for p in policy['platforms']}
    records = candidate.get('platforms', [])
    if len(records) != len(expected) or {p.get('id') for p in records} != set(expected):
        raise ValueError('missing/duplicate/unexpected platform in candidate')
    if {name for name, asset in assets.items() if asset['kind'] == 'binary'} != {
        p['artifact'].format(version=policy['version']) for p in policy['platforms']
    }:
        raise ValueError('unclassified or missing platform binary asset')
    for record in records:
        platform = expected[record['id']]
        name = platform['artifact'].format(version=policy['version'])
        if record.get('artifact') != name or name not in assets or assets[name]['kind'] != 'binary':
            raise ValueError('missing platform binary artifact: ' + name)
        if record.get('source_commit') != commit:
            raise ValueError('platform source commit mismatch')
        if record.get('formats') != platform['formats']:
            raise ValueError(
                f"{record['id']}: formats {record.get('formats')} differ from the policy's {platform['formats']}"
            )
        source = assets.get(record.get('source'))
        if not source or source['kind'] != 'source' or not record.get('notices'):
            raise ValueError('matching source archive and notices required')
    if production:
        problems = production_problems(candidate, policy)
        if problems:
            raise ValueError('; '.join(problems))
    return sorted(assets)


def with_evidence(candidate, evidence, policy):
    """The candidate, with the reviewed evidence of a release evidence file
    (schema 1) added to its platforms' ZIPs and installers.

    Each entry gives one kind of evidence ("kind", one of the policy's
    required_production_evidence), its "result", its "record" (an https link
    to where it is written down), any fields that kind needs (a host test's
    formats, the plug-in formats it tested, and its host, host_version,
    os_build, architecture, sample_rates and channels),
    and "files": each file it covers, by name, with the SHA-256 it was given
    for. The gate refuses evidence given for other bytes as stale."""
    if evidence.get('schema') != 1 or evidence.get('version') != candidate.get('version'):
        raise ValueError(
            f'the evidence file is for version {evidence.get("version")!r} (schema {evidence.get("schema")!r}), '
            f'not {candidate.get("version")} (schema 1)'
        )
    result = copy.deepcopy(candidate)
    targets, shipped = {}, {}
    for record in result['platforms']:
        targets[record['artifact']] = record.setdefault('evidence', {})
        shipped[record['artifact']] = record.get('formats', [])
        for installer in record.get('installers', []):
            qualification = record.setdefault('installer_evidence', {}).setdefault(installer, {})
            targets[installer] = qualification.setdefault('evidence', {})
            shipped[installer] = record.get('formats', [])
    for entry in evidence.get('evidence', []):
        kind = entry.get('kind')
        if kind not in policy['required_production_evidence']:
            raise ValueError(f'evidence of an unknown kind: {kind!r}')
        files = entry.get('files')
        if not isinstance(files, dict) or not files:
            raise ValueError(f'{kind} evidence names no files')
        formats = entry.get('formats')
        if kind == 'host' and (
            not isinstance(formats, list) or not formats or not all(isinstance(f, str) for f in formats)
        ):
            raise ValueError('host evidence names no plug-in formats ("formats": ["vst3"], for example)')
        fields = {key: value for key, value in entry.items() if key not in ('kind', 'files', 'formats')}
        for name, digest in files.items():
            if name not in targets:
                raise ValueError(f'{kind} evidence names {name}, which is not a platform ZIP or installer')
            if not re.fullmatch(r'[0-9a-f]{64}', str(digest)):
                raise ValueError(f'{kind} evidence for {name} gives no SHA-256')
            if kind != 'host':
                if kind in targets[name]:
                    raise ValueError(f'{name} has {kind} evidence twice')
                targets[name][kind] = dict(fields, artifact_sha256=digest)
                continue
            tests = targets[name].setdefault('host', {})
            for format_name in formats:
                if format_name not in shipped[name]:
                    raise ValueError(f'host evidence for {name} names {format_name}, which it does not ship')
                if format_name in tests:
                    raise ValueError(f'{name} has host evidence for {format_name} twice')
                tests[format_name] = dict(fields, artifact_sha256=digest)
    # REL-20 (release audit of 0.22.0): a host version given as ["7.86"]
    # passed the gate and then broke the release notes. Refused here, where
    # the file is read, naming the field, once its files and formats hold.
    examples = {'host': 'REAPER', 'host_version': '7.29', 'os_build': '24G84'}
    for entry in evidence.get('evidence', []):
        for key in HOST_FIELDS if entry['kind'] == 'host' else ():
            if not text(entry.get(key)):
                raise ValueError(
                    f'host evidence for {", ".join(entry["formats"])} gives {key} as {entry.get(key)!r}, '
                    f'not as text ("{key}": "{examples[key]}", for example)'
                )
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument(
        '--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json'
    )
    parser.add_argument('--production', action='store_true')
    parser.add_argument('--evidence', type=Path, help='with --production: the release evidence file')
    args = parser.parse_args()
    policy = load_policy(args.policy)
    candidate = json.loads(args.candidate.read_text())
    if args.evidence:
        candidate = with_evidence(candidate, json.loads(args.evidence.read_text(encoding='utf-8')), policy)
    files = check(candidate, policy, args.root, production=args.production)
    print(
        'Production gate passed'
        if args.production
        else 'Developer-preview integrity gate passed (not production qualification)'
    )
    for name in files:
        print(name)
