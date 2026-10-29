# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate exact candidate assets and reviewed evidence before promotion."""

import argparse
import json
from pathlib import Path
import re
from contract import load_policy, sha256


def check_qualification(record, platform, policy, digest):
    if platform['os'] in ('Windows', 'Darwin') and record.get('signed') is not True:
        raise ValueError('production Windows/macOS artifacts must be signed')
    if platform['os'] == 'Darwin' and record.get('notarized') is not True:
        raise ValueError('production macOS artifacts must be notarized')
    for kind in policy['required_production_evidence']:
        evidence = record.get('evidence', {}).get(kind, {})
        if (
            evidence.get('result') != 'passed'
            or evidence.get('artifact_sha256') != digest
            or not evidence.get('record', '').startswith('https://')
        ):
            raise ValueError('missing/failed/stale production evidence: ' + kind)
    host = record['evidence']['host']
    if (
        not all(host.get(key) for key in ('host', 'host_version', 'os_build'))
        or host.get('architecture') != platform['architecture']
        or set(host.get('sample_rates', [])) != {44100, 48000, 96000}
        or set(host.get('channels', [])) != {1, 2}
    ):
        raise ValueError('host evidence lacks required platform/rate/layout coverage')


def check(candidate, policy, root, production=False):
    if candidate.get('schema') != 1 or candidate.get('version') != policy['version']:
        raise ValueError('candidate version/schema mismatch')
    commit = candidate.get('source_commit', '')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('candidate requires full immutable source commit')
    if production and not policy.get('production_enabled', False):
        raise ValueError('production publication is disabled pending release qualification')
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
    qualified_installers = set()
    for record in records:
        platform = expected[record['id']]
        name = platform['artifact'].format(version=policy['version'])
        if record.get('artifact') != name or name not in assets or assets[name]['kind'] != 'binary':
            raise ValueError('missing platform binary artifact: ' + name)
        if record.get('source_commit') != commit:
            raise ValueError('platform source commit mismatch')
        source = assets.get(record.get('source'))
        if not source or source['kind'] != 'source' or not record.get('notices'):
            raise ValueError('matching source archive and notices required')
        if not production:
            continue
        check_qualification(record, platform, policy, assets[name]['sha256'])
        installers = record.get('installers', [])
        if platform['os'] in ('Windows', 'Darwin') and not installers:
            raise ValueError('missing production installer')
        for installer in installers:
            if installer in qualified_installers or assets.get(installer, {}).get('kind') != 'installer':
                raise ValueError('invalid/duplicate installer association')
            check_qualification(
                record.get('installer_evidence', {}).get(installer, {}), platform, policy, assets[installer]['sha256']
            )
            qualified_installers.add(installer)
    if production and {name for name, asset in assets.items() if asset['kind'] == 'installer'} != qualified_installers:
        raise ValueError('unqualified installer asset')
    return sorted(assets)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument(
        '--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json'
    )
    parser.add_argument('--production', action='store_true')
    args = parser.parse_args()
    files = check(json.loads(args.candidate.read_text()), load_policy(args.policy), args.root, args.production)
    print(
        'Production gate passed'
        if args.production
        else 'Developer-preview integrity gate passed (not production qualification)'
    )
    for name in files:
        print(name)
