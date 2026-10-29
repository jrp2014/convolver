# SPDX-License-Identifier: GPL-2.0-or-later
"""Production promotion must fail closed on incomplete or changed evidence."""

import copy
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
from contract import load_policy, sha256  # noqa: E402


class GateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / 'scripts/release/promotion_gate.py'
        assert path.is_file(), 'immutable publication gate missing'
        spec = importlib.util.spec_from_file_location('promotion_gate', path)
        cls.gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.gate)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.policy = load_policy(ROOT / 'packaging/release-policy.json')
        self.policy['production_enabled'] = True
        self.candidate = {
            'schema': 1,
            'version': self.policy['version'],
            'source_commit': 'a' * 40,
            'assets': [],
            'platforms': [],
        }
        self.add_asset('source.tar.gz', 'source')
        for platform in self.policy['platforms']:
            name = platform['artifact'].format(version=self.policy['version'])
            digest = self.add_asset(name, 'binary')
            evidence = {
                kind: {'result': 'passed', 'artifact_sha256': digest, 'record': 'https://example.org/qualification/1'}
                for kind in self.policy['required_production_evidence']
            }
            evidence['host'].update(
                host='REAPER',
                host_version='test-fixture',
                os_build='fixture',
                architecture=platform['architecture'],
                sample_rates=[44100, 48000, 96000],
                channels=[1, 2],
            )
            self.candidate['platforms'].append(
                {
                    'id': platform['id'],
                    'artifact': name,
                    'source_commit': 'a' * 40,
                    'source': 'source.tar.gz',
                    'notices': True,
                    'signed': True,
                    'notarized': True,
                    'evidence': evidence,
                }
            )
            record = self.candidate['platforms'][-1]
            record['installers'] = []
            record['installer_evidence'] = {}
            if platform['os'] in ('Windows', 'Darwin'):
                installer = name + ('.exe' if platform['os'] == 'Windows' else '.pkg')
                installer_hash = self.add_asset(installer, 'installer')
                record['installers'].append(installer)
                qualification = copy.deepcopy(record)
                for item in qualification['evidence'].values():
                    item['artifact_sha256'] = installer_hash
                record['installer_evidence'][installer] = qualification

    def add_asset(self, name, kind):
        file = self.root / name
        file.write_bytes(name.encode())
        digest = sha256(file)
        self.candidate['assets'].append({'name': name, 'sha256': digest, 'kind': kind})
        return digest

    def test_unassociated_binary(self):
        self.add_asset('unqualified.exe', 'binary')
        with self.assertRaisesRegex(ValueError, 'binary'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_unassociated_installer(self):
        self.add_asset('unqualified-unsigned-setup.exe', 'installer')
        with self.assertRaisesRegex(ValueError, 'installer'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_missing_installer(self):
        self.candidate['platforms'][0]['installers'] = []
        with self.assertRaisesRegex(ValueError, 'installer'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_unsigned_installer(self):
        record = self.candidate['platforms'][0]
        record['installer_evidence'][record['installers'][0]]['signed'] = False
        with self.assertRaisesRegex(ValueError, 'signed'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_changed_installer_with_stale_evidence(self):
        name = self.candidate['platforms'][0]['installers'][0]
        (self.root / name).write_bytes(b'new installer')
        next(a for a in self.candidate['assets'] if a['name'] == name)['sha256'] = sha256(self.root / name)
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_complete_fixture(self):
        self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_changed_hash(self):
        (self.root / self.candidate['platforms'][0]['artifact']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_missing_architecture(self):
        self.candidate['platforms'].pop()
        with self.assertRaisesRegex(ValueError, 'platform'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_failed_host(self):
        self.candidate['platforms'][0]['evidence']['host']['result'] = 'failed'
        with self.assertRaisesRegex(ValueError, 'host'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_evidence_for_different_bytes(self):
        self.candidate['platforms'][0]['evidence']['clean_install']['artifact_sha256'] = 'f' * 64
        with self.assertRaisesRegex(ValueError, 'clean_install'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_unsigned(self):
        self.candidate['platforms'][0]['signed'] = False
        with self.assertRaisesRegex(ValueError, 'signed'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_not_notarized(self):
        self.candidate['platforms'][1]['notarized'] = False
        with self.assertRaisesRegex(ValueError, 'notarized'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_absent_source_and_notices(self):
        for field, value in [('source', 'absent.tar.gz'), ('notices', False)]:
            candidate = copy.deepcopy(self.candidate)
            candidate['platforms'][0][field] = value
            with self.assertRaises(ValueError):
                self.gate.check(candidate, self.policy, self.root, production=True)

    def test_production_disabled(self):
        self.policy['production_enabled'] = False
        with self.assertRaisesRegex(ValueError, 'disabled'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_unsigned_preview_allowed_but_still_requires_all_assets(self):
        self.policy['production_enabled'] = False
        for platform in self.candidate['platforms']:
            platform.update(signed=False, notarized=False, evidence={})
        self.gate.check(self.candidate, self.policy, self.root, production=False)
        self.candidate['platforms'].pop()
        with self.assertRaises(ValueError):
            self.gate.check(self.candidate, self.policy, self.root, production=False)


if __name__ == '__main__':
    unittest.main()
