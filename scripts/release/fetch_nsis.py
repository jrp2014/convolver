# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit release-only NSIS acquisition; verify bytes before extraction."""
import argparse
from pathlib import Path
import urllib.request
from candidate import extract_zip
from contract import load_policy, sha256

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
policy = load_policy(Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
tool = policy['release_tools']['nsis']
args.output.mkdir(parents=True, exist_ok=True)
archive = args.output / 'nsis.zip'
if not archive.exists():
    urllib.request.urlretrieve(tool['url'], archive)
if sha256(archive) != tool['sha256']:
    raise ValueError('NSIS archive hash mismatch')
extract_zip(archive, args.output)
print(args.output / ('nsis-' + tool['version']) / 'makensis.exe')
