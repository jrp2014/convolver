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


class Fixture(unittest.TestCase):
    """A candidate with every production gate met."""

    @classmethod
    def setUpClass(cls):
        path = ROOT / 'scripts/release/promotion_gate.py'
        assert path.is_file(), 'immutable publication gate missing'
        spec = importlib.util.spec_from_file_location('promotion_gate', path)
        cls.gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.gate)

    def setUp(self):
        self.version = load_policy(ROOT / 'packaging/release-policy.json')['version']
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.policy = load_policy(ROOT / 'packaging/release-policy.json')
        self.policy['production_enabled'] = True
        # A full release's files do not say preview (REL-1).
        for platform in self.policy['platforms']:
            platform['artifact'] = platform['artifact'].replace('-preview', '')
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
                    'formats': platform['formats'],
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

    def record(self, identifier):
        return next(r for r in self.candidate['platforms'] if r['id'] == identifier)


class GateTests(Fixture):
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
        record = self.record('macos-arm64')
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

    def test_missing_audio_unit(self):
        # AU-5: a macOS candidate that left the Audio Unit out is incomplete.
        record = next(r for r in self.candidate['platforms'] if r['id'] == 'macos-arm64')
        record['formats'] = ['vst3']
        with self.assertRaisesRegex(ValueError, 'formats'):
            self.gate.check(self.candidate, self.policy, self.root)

    def test_changed_hash(self):
        (self.root / self.candidate['platforms'][0]['artifact']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_missing_architecture(self):
        self.candidate['platforms'].pop()
        with self.assertRaisesRegex(ValueError, 'platform'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_failed_host(self):
        # Windows ships without a named-host test, but a failed one is never ignored.
        self.record('windows-x86_64')['evidence']['host']['result'] = 'failed'
        with self.assertRaisesRegex(
            ValueError, 'windows-x86_64.zip: missing, failed or stale production evidence: host'
        ):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_evidence_for_different_bytes(self):
        self.candidate['platforms'][0]['evidence']['clean_install']['artifact_sha256'] = 'f' * 64
        with self.assertRaisesRegex(ValueError, 'clean_install'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_unsigned(self):
        self.record('macos-x86_64')['signed'] = False
        with self.assertRaisesRegex(ValueError, 'not signed'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_not_notarized(self):
        self.record('macos-arm64')['notarized'] = False
        with self.assertRaisesRegex(ValueError, 'notarized'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_absent_source_and_notices(self):
        for field, value in [('source', 'absent.tar.gz'), ('notices', False)]:
            candidate = copy.deepcopy(self.candidate)
            candidate['platforms'][0][field] = value
            with self.assertRaises(ValueError):
                self.gate.check(candidate, self.policy, self.root, production=True)

    def test_windows_and_linux_ship_unsigned_without_host_tests(self):
        # jrp, 26 November 2026: 1.0 is signed on macOS only, and the Windows
        # and Linux plug-ins ship with named-host tests listed as not verified.
        for identifier in ('windows-x86_64', 'linux-x86_64'):
            record = self.record(identifier)
            record.update(signed=False, notarized=False)
            del record['evidence']['host']
            for qualification in record['installer_evidence'].values():
                qualification.update(signed=False, notarized=False)
                del qualification['evidence']['host']
        self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_macos_still_needs_its_host_test(self):
        del self.record('macos-arm64')['evidence']['host']
        with self.assertRaisesRegex(ValueError, 'macos-arm64.zip: missing, failed or stale production evidence: host'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_every_other_kind_is_still_required(self):
        # Only the unverified kinds may be left out.
        del self.record('windows-x86_64')['evidence']['clean_install']
        with self.assertRaisesRegex(ValueError, 'windows-x86_64.zip: missing, failed or stale production evidence'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

    def test_production_names_neither_preview_nor_unsigned_where_signed(self):
        def unsigned(name):
            stem, dot, extension = name.rpartition('.')
            return f'{stem}-unsigned{dot}{extension}'

        renamed = dict(
            self.candidate,
            platforms=[dict(r, installers=[unsigned(i) for i in r['installers']]) for r in self.candidate['platforms']],
        )
        problems = self.gate.production_problems(renamed, self.policy)
        self.assertIn(f'Convolver-{self.version}-macos-arm64.zip-unsigned.pkg: the file name says unsigned', problems)
        # Windows stays unsigned, so its installer may say so.
        self.assertFalse([p for p in problems if 'windows' in p and 'says unsigned' in p])
        # The policy's names still say preview, so a production run refuses them.
        policy = load_policy(ROOT / 'packaging/release-policy.json')
        record = dict(
            self.record('linux-x86_64'), artifact=policy['platforms'][-1]['artifact'].format(version=self.version)
        )
        problems = self.gate.production_problems(
            dict(
                self.candidate,
                platforms=[record],
                assets=[{'name': record['artifact'], 'sha256': '0' * 64, 'kind': 'binary'}],
            ),
            self.policy,
        )
        self.assertIn(f'Convolver-{self.version}-preview-linux-x86_64.zip: the file name says preview', problems)

    def test_lists_every_problem(self):
        # A dry run shows every gate a candidate has not met, not only the first.
        self.policy['production_enabled'] = False
        for record in self.candidate['platforms']:
            record.update(signed=False, notarized=False, evidence={}, installer_evidence={})
        problems = self.gate.production_problems(self.candidate, self.policy)
        self.assertIn('production_enabled', problems[0])
        for identifier in ('macos-arm64', 'macos-x86_64'):
            self.assertIn(f'Convolver-{self.version}-{identifier}.zip: not signed', problems)
            self.assertIn(f'Convolver-{self.version}-{identifier}.zip: not notarized', problems)
        self.assertFalse([p for p in problems if 'windows' in p and 'not signed' in p])
        missing = [p for p in problems if 'missing, failed or stale' in p]
        # Each platform's ZIP and each macOS and Windows installer.
        self.assertEqual(len(missing), 7)
        self.assertTrue(all('host' not in p for p in missing if 'windows' in p or 'linux' in p))

    def test_platform_without_production_rules(self):
        # A policy from before REL-1 loads, but cannot make a production release.
        del self.policy['platforms'][0]['production']
        with self.assertRaisesRegex(ValueError, 'gives no production rules for windows-x86_64'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)

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


class EvidenceFileTests(Fixture):
    """A release evidence file adds reviewed evidence, bound to hashes (REL-1)."""

    def evidence_file(self):
        """The fixture's evidence, moved out of the candidate into a file."""
        entries = []
        for record in self.candidate['platforms']:
            files = {record['artifact']: record.pop('evidence')}
            for installer, qualification in record['installer_evidence'].items():
                files[installer] = qualification.pop('evidence')
            for name, evidence in files.items():
                for kind, item in evidence.items():
                    fields = {key: value for key, value in item.items() if key != 'artifact_sha256'}
                    entries.append(dict(fields, kind=kind, files={name: item['artifact_sha256']}))
        return {'schema': 1, 'version': self.version, 'evidence': entries}

    def test_adds_the_evidence_to_each_file(self):
        evidence = self.evidence_file()
        with self.assertRaisesRegex(ValueError, 'evidence'):
            self.gate.check(self.candidate, self.policy, self.root, production=True)
        candidate = self.gate.with_evidence(self.candidate, evidence, self.policy)
        self.gate.check(candidate, self.policy, self.root, production=True)
        self.assertNotIn('evidence', self.candidate['platforms'][0])

    def test_one_entry_covers_many_files(self):
        evidence = self.evidence_file()
        review = [e for e in evidence['evidence'] if e['kind'] == 'license_review']
        files = {name: digest for entry in review for name, digest in entry['files'].items()}
        evidence['evidence'] = [e for e in evidence['evidence'] if e['kind'] != 'license_review']
        evidence['evidence'].append(
            {'kind': 'license_review', 'result': 'passed', 'record': 'https://example.org/review', 'files': files}
        )
        candidate = self.gate.with_evidence(self.candidate, evidence, self.policy)
        self.gate.check(candidate, self.policy, self.root, production=True)

    def test_refuses_a_malformed_file(self):
        evidence = self.evidence_file()
        name = self.candidate['platforms'][0]['artifact']
        for change, words in [
            ({'version': '0.0.1'}, 'not ' + self.version),
            ({'schema': 2}, 'schema'),
            ({'evidence': [{'kind': 'vibes', 'files': {name: 'a' * 64}}]}, 'unknown kind'),
            ({'evidence': [{'kind': 'host', 'files': {}}]}, 'names no files'),
            ({'evidence': [{'kind': 'host', 'files': {'source.tar.gz': 'a' * 64}}]}, 'not a platform ZIP'),
            ({'evidence': [{'kind': 'host', 'files': {name: 'abc'}}]}, 'no SHA-256'),
            ({'evidence': [{'kind': 'host', 'files': {name: 'a' * 64}}] * 2}, 'twice'),
        ]:
            with self.subTest(words=words), self.assertRaisesRegex(ValueError, words):
                self.gate.with_evidence(self.candidate, dict(evidence, **change), self.policy)

    def test_evidence_for_other_bytes_is_stale(self):
        evidence = self.evidence_file()
        evidence['evidence'][0]['files'] = dict.fromkeys(evidence['evidence'][0]['files'], 'f' * 64)
        candidate = self.gate.with_evidence(self.candidate, evidence, self.policy)
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.gate.check(candidate, self.policy, self.root, production=True)


if __name__ == '__main__':
    unittest.main()
