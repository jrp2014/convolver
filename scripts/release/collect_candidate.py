# SPDX-License-Identifier: GPL-2.0-or-later
"""Collect per-platform candidate receipts without rebuilding any artifact."""

import argparse
import json
from pathlib import Path

from contract import load_policy
from promotion_gate import check

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
policy = load_policy(Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
assets, platforms, commits = {}, [], set()
for receipt in sorted(args.root.glob('*-candidate.json')):
    candidate = json.loads(receipt.read_text())
    if candidate['version'] != policy['version']:
        raise ValueError('mixed candidate versions')
    commits.add(candidate['source_commit'])
    platforms.append(candidate['platform'])
    for asset in candidate['assets']:
        if asset['name'] in assets and assets[asset['name']] != asset:
            raise ValueError('conflicting candidate assets')
        assets[asset['name']] = asset
if len(commits) != 1:
    raise ValueError('candidate must have one source commit')
result = {
    'schema': 1,
    'version': policy['version'],
    'source_commit': commits.pop(),
    'assets': list(assets.values()),
    'platforms': platforms,
}
check(result, policy, args.root)
if args.output.exists():
    raise ValueError('candidate output already exists')
args.output.write_text(json.dumps(result, indent=2) + '\n')
(args.root / 'SHA256SUMS').write_text(
    ''.join(f'{asset["sha256"]}  {asset["name"]}\n' for asset in sorted(assets.values(), key=lambda a: a['name']))
)
print('Complete developer-preview candidate collected; production gates remain incomplete')
