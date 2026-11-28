# SPDX-License-Identifier: GPL-2.0-or-later
"""Produce the signed macOS candidate from this run's tested, unsigned downloads.

Run on a fresh runner. All programs and installer definitions this script uses
come from macOS or the checked-out revision. The incoming package is data.
"""

import argparse
import json
import shutil
import tempfile
from pathlib import Path

from candidate import extract_zip, macos_installer
from contract import load_policy, platform_policy, sha256
from macos_signing import SigningSession
from symbols import TARGETS
from verify_artifact import inventory, verify

ROOT = Path(__file__).resolve().parents[2]


def checked_input(directory, identifier, policy, commit):
    platform = platform_policy(policy, identifier)
    if platform['os'] != 'Darwin':
        raise ValueError('only macOS candidates can be signed')
    receipt = json.loads((directory / f'{identifier}-candidate.json').read_text())
    record = receipt['platform']
    binary = platform['artifact'].format(version=policy['version'])
    expected = {
        binary: 'binary',
        Path(binary).stem + '-unsigned.pkg': 'installer',
        'Convolver-source.tar.gz': 'source',
        f'{identifier}-artifact-manifest.json': 'manifest',
    }
    assets = receipt['assets']
    if (
        receipt['schema'] != 1
        or receipt['version'] != policy['version']
        or receipt['source_commit'] != commit
        or record['source_commit'] != commit
        or record['id'] != identifier
        or record['artifact'] != binary
        or record['source'] != 'Convolver-source.tar.gz'
        or record['formats'] != platform['formats']
        or record['channel'] != policy['channel']
        or record['signed'] is not False
        or record['notarized'] is not False
        or len(assets) != len(expected)
        or {a['name']: a['kind'] for a in assets} != expected
    ):
        raise ValueError('signing input differs from the requested unsigned candidate')
    for asset in assets:
        path = directory / asset['name']
        if path.is_symlink() or sha256(path) != asset['sha256']:
            raise ValueError(f'candidate digest mismatch: {path.name}')
    return receipt


def sign(directory, identifier, commit, output):
    session = SigningSession()
    policy = load_policy(ROOT / 'packaging/release-policy.json')
    team = policy['macos_signing_team_id']
    receipt = checked_input(directory, identifier, policy, commit)
    record = receipt['platform']
    archive = directory / record['source']
    sidecar = f'{identifier}-artifact-manifest.json'
    manifest = json.loads((directory / sidecar).read_text())
    if (
        manifest['platform'] != identifier
        or manifest['source_commit'] != commit
        or manifest['channel'] != record['channel']
    ):
        raise ValueError('artifact manifest differs from signing request')
    output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        stage = work / 'stage'
        extract_zip(directory / record['artifact'], stage)
        manifest_file = stage / 'artifact-manifest.json'
        if json.loads(manifest_file.read_text()) != manifest:
            raise ValueError('ZIP manifest differs from sidecar')
        verify(stage, manifest, policy, archive)
        if set(manifest['binaries']) != {name for _, name in TARGETS['Darwin']}:
            raise ValueError('unexpected native executables in the signing input')
        with session as signer:
            if signer.team_id != team:
                raise ValueError('signing identity does not belong to the expected Apple team')
            bundle_submission = signer.sign_stage(stage)
            manifest['files'] = inventory(stage)
            manifest_file.write_text(json.dumps(manifest, indent=2) + '\n')
            verify(stage, manifest, policy, archive)
            unsigned = work / 'product.pkg'
            macos_installer(stage, work / 'installer', ROOT, policy, record['formats'], unsigned)
            product = output / (Path(record['artifact']).stem + '.pkg')
            installer_submission = signer.sign_installer(unsigned, product)
        shutil.make_archive(str(output / Path(record['artifact']).stem), 'zip', root_dir=stage)
        shutil.copyfile(manifest_file, output / sidecar)
    shutil.copyfile(archive, output / archive.name)
    signed = {'signed': True, 'notarized': True, 'team_id': team}
    record.update(
        signed,
        notary_submission_id=bundle_submission,
        installers=[product.name],
        installer_evidence={product.name: dict(signed, notary_submission_id=installer_submission)},
    )
    for asset in receipt['assets']:
        if asset['kind'] == 'installer':
            asset['name'] = product.name
        asset['sha256'] = sha256(output / asset['name'])
    (output / f'{identifier}-candidate.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--platform', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sign(args.input, args.platform, args.commit, args.output)
