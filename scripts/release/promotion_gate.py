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
    # An unverified kind is not required, but a result given for it must
    # still have passed, on these bytes: a failed test is never ignored.
    required = [
        kind for kind in policy['required_production_evidence'] if kind not in rules['unverified'] or kind in evidence
    ]
    missing = [
        kind
        for kind in required
        if evidence.get(kind, {}).get('result') != 'passed'
        or evidence[kind].get('artifact_sha256') != digest
        or not str(evidence[kind].get('record', '')).startswith('https://')
    ]
    if missing:
        problems.append(f'{label}: missing, failed or stale production evidence: {", ".join(missing)}')
    if 'host' in required and 'host' not in missing:
        host = evidence['host']
        if (
            not all(host.get(key) for key in ('host', 'host_version', 'os_build'))
            or host.get('architecture') != platform['architecture']
            or set(host.get('sample_rates', [])) != {44100, 48000, 96000}
            or set(host.get('channels', [])) != {1, 2}
        ):
            problems.append(f'{label}: host evidence lacks required platform/rate/layout coverage')
    return problems


def production_problems(candidate, policy):
    """Every production gate that a candidate which passed `check` does not meet."""
    problems = []
    if not policy.get('production_enabled', False):
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
    host, host_version, os_build, architecture, sample_rates and channels),
    and "files": each file it covers, by name, with the SHA-256 it was given
    for. The gate refuses evidence given for other bytes as stale."""
    if evidence.get('schema') != 1 or evidence.get('version') != candidate.get('version'):
        raise ValueError(
            f'the evidence file is for version {evidence.get("version")!r} (schema {evidence.get("schema")!r}), '
            f'not {candidate.get("version")} (schema 1)'
        )
    result = copy.deepcopy(candidate)
    targets = {}
    for record in result['platforms']:
        targets[record['artifact']] = record.setdefault('evidence', {})
        for installer in record.get('installers', []):
            qualification = record.setdefault('installer_evidence', {}).setdefault(installer, {})
            targets[installer] = qualification.setdefault('evidence', {})
    for entry in evidence.get('evidence', []):
        kind = entry.get('kind')
        if kind not in policy['required_production_evidence']:
            raise ValueError(f'evidence of an unknown kind: {kind!r}')
        files = entry.get('files')
        if not isinstance(files, dict) or not files:
            raise ValueError(f'{kind} evidence names no files')
        fields = {key: value for key, value in entry.items() if key not in ('kind', 'files')}
        for name, digest in files.items():
            if name not in targets:
                raise ValueError(f'{kind} evidence names {name}, which is not a platform ZIP or installer')
            if not re.fullmatch(r'[0-9a-f]{64}', str(digest)):
                raise ValueError(f'{kind} evidence for {name} gives no SHA-256')
            if kind in targets[name]:
                raise ValueError(f'{name} has {kind} evidence twice')
            targets[name][kind] = dict(fields, artifact_sha256=digest)
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
