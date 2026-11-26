# SPDX-License-Identifier: GPL-2.0-or-later
"""Preparing a draft pre-release must refuse any run, tag or file that is not
the candidate it claims to be (REL-8), and attach only the published files:
every platform's packages and the source (REL-9). A full release must also
meet every production gate that jrp's current requirements ask for, and its
notes say what is unsigned or not verified (REL-1)."""

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import prepare_release  # noqa: E402
from contract import load_policy, sha256  # noqa: E402


def git(directory, *args):
    return subprocess.run(
        ['git', '-C', str(directory), *args], capture_output=True, text=True, check=True
    ).stdout.strip()


class Fixture(unittest.TestCase):
    """A passed candidate run on the current version's tag."""

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.temp = Path(temp.name)
        self.version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        self.tag = 'v' + self.version
        # A checkout of the tag: VERSION and the release policy, committed.
        self.tagged = self.temp / 'tagged'
        (self.tagged / 'packaging').mkdir(parents=True)
        shutil.copyfile(ROOT / 'VERSION', self.tagged / 'VERSION')
        # Every tag before REL-1 has a policy without production rules.
        policy = json.loads((ROOT / 'packaging/release-policy.json').read_text(encoding='utf-8'))
        for platform in policy['platforms']:
            platform.pop('production', None)
        (self.tagged / 'packaging/release-policy.json').write_text(json.dumps(policy, indent=2))
        git(self.tagged, 'init', '-q')
        git(self.tagged, 'add', '.')
        git(self.tagged, '-c', 'user.name=t', '-c', 'user.email=t@example.org', 'commit', '-qm', 'fixture')
        self.commit = git(self.tagged, 'rev-parse', 'HEAD')
        self.policy = load_policy(self.tagged / 'packaging/release-policy.json')
        # The complete-preview-candidate files, as collect_candidate.py leaves them.
        self.root = self.temp / 'candidate'
        self.root.mkdir()
        assets = [self.asset('Convolver-source.tar.gz', 'source')]
        platforms = []
        for platform in self.policy['platforms']:
            name = platform['artifact'].format(version=self.version)
            assets.append(self.asset(name, 'binary'))
            installers = []
            if platform['os'] in ('Darwin', 'Windows'):
                suffix = '-unsigned.pkg' if platform['os'] == 'Darwin' else '-unsigned-setup.exe'
                installers = [name.replace('.zip', suffix)]
                assets.append(self.asset(installers[0], 'installer'))
            assets.append(self.asset(platform['id'] + '-artifact-manifest.json', 'manifest'))
            platforms.append(
                {
                    'id': platform['id'],
                    'artifact': name,
                    'source_commit': self.commit,
                    'formats': platform['formats'],
                    'source': 'Convolver-source.tar.gz',
                    'notices': ['licenses/Convolver.txt'],
                    'installers': installers,
                }
            )
        candidate = {
            'schema': 1,
            'version': self.version,
            'source_commit': self.commit,
            'assets': assets,
            'platforms': platforms,
        }
        (self.root / 'candidate.json').write_text(json.dumps(candidate))
        (self.root / 'SHA256SUMS').write_text(
            ''.join(f'{a["sha256"]}  {a["name"]}\n' for a in sorted(assets, key=lambda a: a['name']))
        )
        self.run_record = {
            'id': 42,
            'path': '.github/workflows/release.yml',
            'event': 'workflow_dispatch',
            'conclusion': 'success',
            'head_branch': self.tag,
            'head_sha': self.commit,
            'html_url': 'https://github.com/owner/convolver/actions/runs/42',
        }
        self.output = self.temp / 'out'

    def asset(self, name, kind):
        path = self.root / name
        path.write_bytes(name.encode() * 3)
        return {'name': name, 'sha256': sha256(path), 'kind': kind}


class PrepareReleaseTests(Fixture):
    def prepare(self, **changes):
        arguments = {
            'run': self.run_record,
            'root': self.root,
            'tagged': self.tagged,
            'tag': self.tag,
            'repository': 'owner/convolver',
            'qualification': 'Qualified on a named Mac.',
            'output': self.output,
        }
        arguments.update(changes)
        names, problems = prepare_release.prepare(**arguments)
        self.assertEqual(problems, [])
        return names

    def test_attaches_every_platform_and_the_source_but_no_manifest(self):
        # REL-9: Windows and Linux joined macOS from the second preview.
        names = self.prepare()
        version = self.version
        self.assertEqual(
            names,
            [
                f'Convolver-{version}-preview-macos-arm64.zip',
                f'Convolver-{version}-preview-macos-arm64-unsigned.pkg',
                f'Convolver-{version}-preview-macos-x86_64.zip',
                f'Convolver-{version}-preview-macos-x86_64-unsigned.pkg',
                f'Convolver-{version}-preview-windows-x86_64.zip',
                f'Convolver-{version}-preview-windows-x86_64-unsigned-setup.exe',
                f'Convolver-{version}-preview-linux-x86_64.zip',
                'Convolver-source.tar.gz',
            ],
        )
        sums = (self.output / 'SHA256SUMS').read_text().splitlines()
        self.assertEqual([line.split('  ')[1] for line in sums], names)
        for line in sums:
            digest, name = line.split('  ')
            self.assertEqual(digest, sha256(self.root / name))
        self.assertEqual((self.output / 'title.txt').read_text(), f'Convolver {version} (unsigned developer preview)\n')

    def test_notes_give_each_attached_sum_and_no_other(self):
        self.prepare()
        text = (self.output / 'notes.md').read_text()
        for line in (self.output / 'SHA256SUMS').read_text().splitlines():
            digest, name = line.split('  ')
            self.assertIn(f'| `{name}` |', text)
            self.assertIn(f'`{digest}`', text)
        self.assertNotIn('artifact-manifest.json', text)
        # Each system's own checksum command, and what Windows and Linux lack.
        for words in ('shasum -a 256', 'Get-FileHash', 'sha256sum', 'SmartScreen', 'Visual C++'):
            self.assertIn(words, text)
        self.assertIn('On Linux the VST3 plug-in has no window yet', text)
        # The Windows window (VST3-12) has only been checked on CI.
        self.assertIn('Nobody has tried it in a Windows host', text)
        self.assertIn(
            f'https://github.com/owner/convolver/blob/{self.tag}/docs/use/first-sound-in-a-host.md#optional-load-a-preset',
            text,
        )
        self.assertIn('Nobody has used them in a host yet', text)
        self.assertNotIn('macOS only', text)
        self.assertIn('Qualified on a named Mac.', text)
        self.assertIn(self.commit, text)
        self.assertIn('https://github.com/owner/convolver/actions/runs/42', text)
        self.assertIn(f'https://github.com/owner/convolver/blob/{self.tag}/docs/use/install.md#unsigned-previews', text)
        # What was not checked before publication (jrp's decision of 8 November 2026).
        self.assertIn('Not yet checked: listening in any host', text)
        # VST3-17 is fixed; the release notes' changes list says what is checked.
        self.assertNotIn('Audacity 4 does not keep it', text)
        # jrp checked the fix in Audacity on 21 November 2026; the notes say what is still unchecked.
        self.assertNotIn('not yet in Audacity', text)
        self.assertIn('checked in Audacity 4.0.2 on macOS', text)
        self.assertIn('Reload and reloading when the file changes are checked only by automated tests', text)
        # The new engine was published without a host listening session (jrp, 9 November 2026),
        # and some older projects open at the new default Latency (PERF-2).
        self.assertIn('nobody has listened to it in a host yet', text)
        self.assertIn('Some projects saved with 0.12.12', text)
        self.assertIn(f'https://github.com/owner/convolver/blob/{self.tag}/docs/use/plugins.md#settings', text)
        self.assertIn(f'https://github.com/owner/convolver/blob/{self.tag}/CHANGELOG.md', text)
        # What earlier previews refuse (PLUG-35, jrp's decision of 12 November 2026;
        # the Gain mode's state), and what they play instead in the Audio Unit and LV2.
        self.assertIn('In the Audio Unit and LV2, previews before 0.13.20 have no Gain mode', text)
        self.assertIn('refused as damaged by the first preview, 0.12.12', text)
        self.assertIn('Gain mode Estimate or No clipping is refused by every preview before 0.13.20', text)
        # LV2-9's 100 ms wait mitigates, not rules out, the JUCE host crash (jrp's review of 19 November 2026).
        self.assertIn('this mitigates a host crash rather than ruling it out', text)
        # What each preview added that only automated tests have checked (0.13.20, 0.14, 0.18.10, 0.18.12 and since).
        self.assertIn('Checked only by automated tests: new since 0.18.12, everything under What changed', text)
        self.assertIn('new in 0.14, the quicker restore of the playing filter', text)
        # VST3-26 changed how the two VST3 halves keep in step after every VST3 host check.
        self.assertIn('which every VST3 check above predates', text)
        self.assertIn('https://github.com/owner/convolver/releases/tag/v0.18.12', text)
        # The host checks not repeated on this version stay listed as unverified (jrp, 8 November 2026);
        # what jrp's live session checked on 0.18.10 is credited to 0.18.10 only.
        self.assertIn('Checked on 0.18.10, built from its tag on one Mac', text)
        self.assertIn('To retest on this preview, with what 0.18.10 showed where it was tried: in REAPER', text)
        # Check 10 ran on the 0.16.14 candidate; the chosen-file path changed after it (status.md).
        self.assertIn('a configuration list chosen before playing (failed on 0.18.10, fixed in 0.18.12)', text)
        self.assertIn('with the 0.16.14 candidate', text)
        # What a crossover or mono filter gets in Ardour (HOST-10).
        self.assertIn(f'https://github.com/owner/convolver/blob/{self.tag}/docs/use/plugins.md#ardour', text)
        self.assertIn('new in 0.13.20, the Gain mode', text)
        # What changed since the previous preview, one line per change.
        self.assertIn(f'Since {prepare_release.PREVIOUS_PREVIEW}:', text)
        for change in prepare_release.CHANGES:
            self.assertIn(f'- {change}\n', text)
        # How to report a problem, with what makes a report useful.
        self.assertIn('## Report a problem', text)
        self.assertIn(f'https://github.com/owner/convolver/blob/{self.tag}/docs/use/report-a-problem.md', text)
        for words in ('Copy Diagnostics', 'Convolver log', 'host and version', 'playback', 'export or reopening'):
            self.assertIn(words, text)
        self.assertIn(
            f'https://github.com/owner/convolver/blob/{self.tag}/docs/use/report-a-problem.md#2-collect-convolvers-messages',
            text,
        )
        # Preview users are asked for feedback as well as problems.
        self.assertIn('https://github.com/owner/convolver/issues/new?template=preview_feedback.yml', text)

    def test_refuses_a_run_that_is_not_a_manual_successful_candidate_run_on_the_tag(self):
        for key, value, words in [
            ('path', '.github/workflows/vst3.yml', 'not the candidate workflow'),
            ('event', 'push', 'not by hand'),
            ('conclusion', 'failure', "not 'success'"),
            ('head_branch', 'main', 'not the tag'),
        ]:
            with self.subTest(key=key):
                run = dict(self.run_record, **{key: value})
                with self.assertRaisesRegex(ValueError, words):
                    self.prepare(run=run)
        self.assertFalse(self.output.exists())

    def test_refuses_a_tag_that_does_not_match(self):
        with self.assertRaisesRegex(ValueError, 'not a vX.Y.Z tag'):
            self.prepare(tag='preview', run=dict(self.run_record, head_branch='preview'))
        with self.assertRaisesRegex(ValueError, 'holds VERSION'):
            self.prepare(tag='v0.0.1', run=dict(self.run_record, head_branch='v0.0.1'))
        with self.assertRaisesRegex(ValueError, 'but run 42 built'):
            self.prepare(run=dict(self.run_record, head_sha='b' * 40))

    def test_refuses_a_changed_or_missing_file(self):
        name = f'Convolver-{self.version}-preview-macos-arm64-unsigned.pkg'
        (self.root / name).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'changed asset hash'):
            self.prepare()

    def test_refuses_sums_that_differ_from_the_record(self):
        with (self.root / 'SHA256SUMS').open('a') as sums:
            sums.write(f'{"0" * 64}  extra.txt\n')
        (self.root / 'extra.txt').write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'does not match its SHA256SUMS line'):
            self.prepare()
        # A correct sum for a file that the candidate record does not list.
        lines = (self.root / 'SHA256SUMS').read_text().splitlines()[:-1]
        lines.append(f'{sha256(self.root / "extra.txt")}  extra.txt')
        (self.root / 'SHA256SUMS').write_text('\n'.join(lines) + '\n')
        with self.assertRaisesRegex(ValueError, 'list different files'):
            self.prepare()

    def test_needs_the_qualification_result(self):
        with self.assertRaisesRegex(ValueError, 'REL-6'):
            self.prepare(qualification='  ')


class ProductionTests(Fixture):
    """--production: a full release, judged by this checkout's requirements."""

    def production(self, **changes):
        arguments = {
            'run': self.run_record,
            'root': self.root,
            'tagged': self.tagged,
            'tag': self.tag,
            'repository': 'owner/convolver',
            'qualification': 'Qualified on a named Mac.',
            'output': self.output,
            'production': True,
        }
        arguments.update(changes)
        return prepare_release.prepare(**arguments)

    def test_refuses_while_production_is_disabled(self):
        # production_enabled is false until jrp authorizes a release.
        with self.assertRaisesRegex(ValueError, 'not ready for a production release: .*production_enabled'):
            self.production()
        self.assertFalse(self.output.exists())

    def test_dry_run_lists_every_unmet_gate_and_writes_the_notes(self):
        names, problems = self.production(dry_run=True)
        version = self.version
        self.assertIn('production_enabled', problems[0])
        for platform in ('macos-arm64', 'macos-x86_64'):
            self.assertIn(f'Convolver-{version}-preview-{platform}.zip: not signed', problems)
            self.assertIn(f'Convolver-{version}-preview-{platform}.zip: not notarized', problems)
            self.assertIn(f'Convolver-{version}-preview-{platform}-unsigned.pkg: the file name says unsigned', problems)
        # jrp, 26 November 2026: Windows ships unsigned, and Windows and Linux without a named-host test.
        self.assertFalse([p for p in problems if 'windows' in p and ('not signed' in p or 'says unsigned' in p)])
        for problem in problems:
            if 'missing, failed or stale' in problem and ('windows' in problem or 'linux' in problem):
                self.assertNotIn('host', problem)
        self.assertIn(f'Convolver-{version}-preview-linux-x86_64.zip: the file name says preview', problems)
        self.assertEqual(len(names), 8)
        self.assertEqual((self.output / 'title.txt').read_text(), f'Convolver {version}\n')
        text = (self.output / 'notes.md').read_text()
        self.assertNotIn('preview.**', text)
        self.assertNotIn('Unsigned developer preview', text)
        self.assertIn('The macOS files are signed with an Apple Developer ID and notarized.', text)
        self.assertIn('The Windows files are not signed with a code-signing certificate', text)
        self.assertIn('- The Windows files are not signed: Windows shows a SmartScreen warning', text)
        self.assertIn('- Not verified in a named host on Windows and Linux:', text)
        self.assertNotIn('- Unsigned: macOS asks you', text)
        self.assertNotIn('An early preview, published before its last checks', text)
        self.assertNotIn('#unsigned-previews', text)
        # What every release's notes give.
        for words in ('shasum -a 256', 'Get-FileHash', 'sha256sum', 'Visual C++', '## Report a problem'):
            self.assertIn(words, text)
        for line in (self.output / 'SHA256SUMS').read_text().splitlines():
            digest, name = line.split('  ')
            self.assertIn(f'| `{name}` | ', text)
            self.assertIn(f'`{digest}`', text)

    def test_requirements_come_from_this_checkout_not_the_tag(self):
        # A tag made before a decision is judged by the decision: the tag's
        # policy without production rules still gets this checkout's.
        policy = json.loads((self.tagged / 'packaging/release-policy.json').read_text())
        for platform in policy['platforms']:
            platform['production'] = {'signed': True, 'unverified': []}
        requirements = self.temp / 'requirements'
        (requirements / 'packaging').mkdir(parents=True)
        shutil.copyfile(ROOT / 'VERSION', requirements / 'VERSION')
        (requirements / 'packaging/release-policy.json').write_text(json.dumps(policy))
        _, problems = self.production(dry_run=True, requirements=requirements / 'packaging/release-policy.json')
        self.assertIn(f'Convolver-{self.version}-preview-windows-x86_64.zip: not signed', problems)
        _, problems = self.production(dry_run=True)
        self.assertNotIn(f'Convolver-{self.version}-preview-windows-x86_64.zip: not signed', problems)

    def test_evidence_file_is_bound_to_the_files(self):
        name = f'Convolver-{self.version}-preview-linux-x86_64.zip'
        evidence = {
            'schema': 1,
            'version': self.version,
            'evidence': [
                {
                    'kind': kind,
                    'result': 'passed',
                    'record': 'https://example.org/evidence',
                    'files': {name: sha256(self.root / name)},
                }
                for kind in self.policy['required_production_evidence']
                if kind != 'host'
            ],
        }
        _, problems = self.production(dry_run=True, evidence=evidence)
        self.assertFalse([p for p in problems if p.startswith(name + ': missing')])
        evidence['evidence'][0]['files'][name] = 'f' * 64
        _, problems = self.production(dry_run=True, evidence=evidence)
        self.assertIn(f'{name}: missing, failed or stale production evidence: automated', problems)
        with self.assertRaisesRegex(ValueError, 'not a platform ZIP or installer'):
            self.production(
                dry_run=True,
                evidence=dict(
                    evidence, evidence=[dict(evidence['evidence'][0], files={'Convolver-source.tar.gz': 'a' * 64})]
                ),
            )


if __name__ == '__main__':
    unittest.main()
