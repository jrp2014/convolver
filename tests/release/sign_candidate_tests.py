# SPDX-License-Identifier: GPL-2.0-or-later
"""Trace the tested input through signing, sealing and collection without credentials."""

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import sign_candidate as signing  # noqa: E402
from contract import load_policy, platform_policy, sha256  # noqa: E402


class SignedCandidate(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.input = self.root / 'input'
        self.input.mkdir()
        self.output = self.root / 'result'
        self.commit = 'a' * 40
        self.platform = 'macos-arm64'
        self.policy = load_policy(ROOT / 'packaging/release-policy.json')
        platform = platform_policy(self.policy, self.platform)
        self.zip = platform['artifact'].format(version=self.policy['version'])
        self.sidecar = self.platform + '-artifact-manifest.json'
        self.receipt = self.input / (self.platform + '-candidate.json')
        stage = self.root / 'stage'
        stage.mkdir()
        (stage / 'sample').write_bytes(b'unsigned input')
        manifest = {
            'source_commit': self.commit,
            'channel': self.policy['channel'],
            'platform': self.platform,
            'files': signing.inventory(stage),
            'binaries': {n: {} for _, n in signing.TARGETS['Darwin']},
        }
        (stage / 'artifact-manifest.json').write_text(json.dumps(manifest))
        shutil.copyfile(stage / 'artifact-manifest.json', self.input / self.sidecar)
        shutil.make_archive(str(self.input / Path(self.zip).stem), 'zip', root_dir=stage)
        source = 'Convolver-source.tar.gz'
        installer = Path(self.zip).stem + '-unsigned.pkg'
        (self.input / source).write_bytes(b'corresponding source')
        (self.input / installer).write_bytes(b'unsigned package')
        record = {
            'id': self.platform,
            'source': source,
            'source_commit': self.commit,
            'artifact': self.zip,
            'channel': self.policy['channel'],
            'formats': platform['formats'],
            'signed': False,
            'notarized': False,
            'installers': [installer],
        }
        assets = [
            {'kind': kind, 'name': name, 'sha256': sha256(self.input / name)}
            for name, kind in [
                (self.zip, 'binary'),
                (source, 'source'),
                (self.sidecar, 'manifest'),
                (installer, 'installer'),
            ]
        ]
        self.receipt.write_text(
            json.dumps(
                {
                    'version': self.policy['version'],
                    'schema': 1,
                    'source_commit': self.commit,
                    'assets': assets,
                    'platform': record,
                }
            )
        )

    def run_sign(self):
        signing.sign(self.input, self.platform, self.commit, self.output)

    def test_only_exact_input_reaches_the_keychain(self):
        with patch.object(signing, 'SigningSession') as session:
            with self.assertRaisesRegex(ValueError, 'requested unsigned candidate'):
                signing.sign(self.input, self.platform, 'b' * 40, self.output)
            (self.input / self.zip).write_bytes(b'changed download')
            with self.assertRaisesRegex(ValueError, 'digest mismatch'):
                self.run_sign()
            session.return_value.__enter__.assert_not_called()

    def test_final_receipt_hashes_stapled_downloads(self):
        def sign_bundle(stage):
            (stage / 'sample').write_bytes(b'signed and stapled bundle')
            return 'bundle-submission'

        def build_installer(stage, generated, source, policy, formats, unsigned):
            self.assertEqual(source, ROOT)
            self.assertEqual(
                json.loads((stage / 'artifact-manifest.json').read_text())['files'], signing.inventory(stage)
            )
            unsigned.write_bytes((stage / 'sample').read_bytes())

        def sign_installer(unsigned, product):
            product.write_bytes(unsigned.read_bytes() + b' and signed and stapled installer')
            return 'installer-submission'

        with patch.object(signing, 'SigningSession') as session, patch.object(signing, 'verify'), patch.object(
            signing, 'macos_installer', side_effect=build_installer
        ):
            signer = session.return_value.__enter__.return_value
            signer.team_id = self.policy['macos_signing_team_id']
            signer.sign_stage.side_effect = sign_bundle
            signer.sign_installer.side_effect = sign_installer
            self.run_sign()
        receipt = json.loads((self.output / self.receipt.name).read_text())
        record = receipt['platform']
        self.assertTrue(record['signed'] and record['notarized'])
        self.assertEqual(record['notary_submission_id'], 'bundle-submission')
        (product,) = record['installers']
        self.assertNotIn('-unsigned', product)
        self.assertEqual(record['installer_evidence'][product]['notary_submission_id'], 'installer-submission')
        self.assertEqual(
            {a['name']: a['sha256'] for a in receipt['assets']},
            {p.name: sha256(p) for p in self.output.iterdir() if p.name != self.receipt.name},
        )
        unpacked = self.root / 'relocated'
        signing.extract_zip(self.output / self.zip, unpacked)
        self.assertEqual((unpacked / 'artifact-manifest.json').read_bytes(), (self.output / self.sidecar).read_bytes())
        self.assertEqual(signing.inventory(unpacked), json.loads((self.output / self.sidecar).read_text())['files'])


if __name__ == '__main__':
    unittest.main()
