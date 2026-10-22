# SPDX-License-Identifier: GPL-2.0-or-later
"""Release policy rejects ambiguous or incomplete candidate contracts."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = ROOT / 'scripts/release/contract.py'
        assert script.is_file(), 'release contract validator is missing'
        spec = importlib.util.spec_from_file_location('contract', script)
        cls.contract = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.contract)
        cls.policy = cls.contract.load_policy(ROOT / 'packaging/release-policy.json')

    def test_repository_policy(self):
        self.contract.validate_policy(self.policy, ROOT)

    def test_missing_pin(self):
        policy = copy.deepcopy(self.policy)
        del policy['dependencies']['fftw']['sha256']
        with self.assertRaisesRegex(ValueError, 'sha256'):
            self.contract.validate_policy(policy, ROOT)

    def test_mutable_sdk_submodule(self):
        policy = copy.deepcopy(self.policy)
        policy['dependencies']['vst3-base']['revision'] = 'main'
        with self.assertRaisesRegex(ValueError, 'revision'):
            self.contract.validate_policy(policy, ROOT)

    def test_mutable_format_sdk(self):
        for name in ('lv2', 'audiounitsdk'):
            policy = copy.deepcopy(self.policy)
            del policy['dependencies'][name]['revision']
            with self.assertRaisesRegex(ValueError, f'{name}: missing immutable revision'):
                self.contract.validate_policy(policy, ROOT)

    def test_unknown_format(self):
        policy = copy.deepcopy(self.policy)
        policy['dependencies']['lv2']['formats'] = ['clap']
        with self.assertRaisesRegex(ValueError, 'formats'):
            self.contract.validate_policy(policy, ROOT)

    def test_missing_format_pin(self):
        policy = copy.deepcopy(self.policy)
        del policy['dependencies']['audiounitsdk']
        with self.assertRaisesRegex(ValueError, 'missing or unexpected'):
            self.contract.validate_policy(policy, ROOT)

    def test_dependencies_follow_formats_and_backend(self):
        used = self.contract.used_dependencies
        vst3 = {'vst3sdk', 'vst3-base', 'vst3-cmake', 'vst3-pluginterfaces', 'vst3-public-sdk'}
        self.assertEqual(used(self.policy, ('vst3',), 'OOURA'), vst3 | {'libsndfile'})
        self.assertEqual(used(self.policy, ('vst3',), 'FFTW'), vst3 | {'libsndfile', 'fftw'})
        self.assertEqual(used(self.policy, ('lv2',), 'OOURA'), {'lv2', 'libsndfile'})
        self.assertEqual(used(self.policy, ('au',), 'ACCELERATE'), {'audiounitsdk', 'libsndfile'})
        self.assertEqual(used(self.policy, ('vst3', 'lv2', 'au'), 'AUTO'),
                         vst3 | {'lv2', 'audiounitsdk', 'libsndfile'})
        with self.assertRaisesRegex(ValueError, 'unknown plug-in formats'):
            used(self.policy, ('clap',))

    def test_conflicting_version(self):
        policy = copy.deepcopy(self.policy)
        policy['version'] = '99.0.0'
        with self.assertRaisesRegex(ValueError, 'version'):
            self.contract.validate_policy(policy, ROOT)

    def test_duplicate_artifact(self):
        policy = copy.deepcopy(self.policy)
        policy['platforms'][1]['artifact'] = policy['platforms'][0]['artifact']
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.contract.validate_policy(policy, ROOT)

    def test_missing_architecture_artifact(self):
        policy = copy.deepcopy(self.policy)
        del policy['platforms'][0]['artifact']
        with self.assertRaisesRegex(ValueError, 'artifact'):
            self.contract.validate_policy(policy, ROOT)

    def test_tested_without_evidence(self):
        policy = copy.deepcopy(self.policy)
        policy['platforms'][0]['status'] = 'tested'
        with self.assertRaisesRegex(ValueError, 'evidence'):
            self.contract.validate_policy(policy, ROOT)


if __name__ == '__main__':
    unittest.main()
