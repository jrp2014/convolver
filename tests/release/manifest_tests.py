# SPDX-License-Identifier: GPL-2.0-or-later
"""Release policy rejects ambiguous or incomplete candidate contracts."""

import copy
import importlib.util
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

    def test_pinned_version_is_refused(self):
        # CI-6: the policy says where a dependency comes from; builds resolve
        # and record the release (scripts/release/resolve.py).
        for key in ('sha256', 'revision', 'version'):
            policy = copy.deepcopy(self.policy)
            policy['dependencies']['fftw'][key] = 'x'
            with self.assertRaisesRegex(ValueError, f'does not pin a {key}'):
                self.contract.validate_policy(policy, ROOT)

    def test_missing_upstream(self):
        policy = copy.deepcopy(self.policy)
        del policy['dependencies']['lv2']['upstream']
        with self.assertRaisesRegex(ValueError, 'lv2: missing https upstream'):
            self.contract.validate_policy(policy, ROOT)

    def test_submodule_fetched_by_commit(self):
        policy = copy.deepcopy(self.policy)
        policy['dependencies']['vst3-base']['url'] = 'https://example.org/vst3_base-{version}.tar.gz'
        with self.assertRaisesRegex(ValueError, 'vst3-base: a submodule archive is fetched by commit'):
            self.contract.validate_policy(policy, ROOT)
        policy = copy.deepcopy(self.policy)
        policy['dependencies']['vst3-base']['submodule_of'] = 'vst3-cmake'
        with self.assertRaisesRegex(ValueError, 'submodule_of must name a top-level dependency'):
            self.contract.validate_policy(policy, ROOT)

    def test_records_name_exact_releases(self):
        record = {
            'version': '1.18.10',
            'revision': '5' * 40,
            'url': 'https://example.org/lv2.tar.gz',
            'sha256': '0' * 64,
            'license': 'ISC',
            'archive': 'lv2.tar.gz',
            'directory': 'lv2',
        }
        self.contract.validate_records({'lv2': record})
        for key, value, message in (
            ('sha256', 'x', 'sha256'),
            ('revision', 'main', 'immutable revision'),
            ('url', 'http://example.org', 'URL'),
        ):
            with self.assertRaisesRegex(ValueError, message):
                self.contract.validate_records({'lv2': dict(record, **{key: value})})
        with self.assertRaisesRegex(ValueError, 'no record for dependencies'):
            self.contract.with_records(self.policy, {'lv2': record})

    def test_unknown_format(self):
        policy = copy.deepcopy(self.policy)
        policy['dependencies']['lv2']['formats'] = ['clap']
        with self.assertRaisesRegex(ValueError, 'formats'):
            self.contract.validate_policy(policy, ROOT)

    def test_missing_format_dependency(self):
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
        self.assertEqual(used(self.policy, ('vst3', 'lv2', 'au'), 'AUTO'), vst3 | {'lv2', 'audiounitsdk', 'libsndfile'})
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
