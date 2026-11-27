# SPDX-License-Identifier: GPL-2.0-or-later
"""Preparing a draft pre-release must refuse any run, tag or file that is not
the candidate it claims to be (REL-8), and attach only the published files:
every platform's packages and the source (REL-9). A full release must also
meet every production gate that jrp's current requirements ask for, and its
notes say what is unsigned or not verified (REL-1)."""

import json
import os
import re
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

# Pre-commit runs these tests as a Git hook, whose GIT_DIR or GIT_INDEX_FILE
# would point the scratch repositories' commands, the script's among them,
# at the real one.
for key in [key for key in os.environ if key.startswith('GIT_')]:
    del os.environ[key]


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
                f'Convolver-{version}-macos-arm64.zip',
                f'Convolver-{version}-macos-arm64-unsigned.pkg',
                f'Convolver-{version}-macos-x86_64.zip',
                f'Convolver-{version}-macos-x86_64-unsigned.pkg',
                f'Convolver-{version}-windows-x86_64.zip',
                f'Convolver-{version}-windows-x86_64-unsigned-setup.exe',
                f'Convolver-{version}-linux-x86_64.zip',
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
        # REL-11: where the clean-machine checks ran, and the gap to the minimum systems.
        self.assertIn('ran under the x64 emulation built into Windows', text)
        self.assertIn('nothing has run on macOS 14 itself', text)
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
        self.assertIn('a Gain mode other than Manual is refused by every preview before 0.13.20', text)
        # LV2-9's 100 ms wait mitigates, not rules out, the JUCE host crash (jrp's review of 19 November 2026).
        self.assertIn('this mitigates a host crash rather than ruling it out', text)
        # What each preview added that only automated tests have checked (0.13.20, 0.14, 0.18.10, 0.18.12,
        # 0.18.29 and since the previous preview, which the notes page names).
        previous = prepare_release.notes_text()['previous']
        self.assertIn(f'Checked only by automated tests: new since {previous}, everything under What changed', text)
        self.assertIn(f'Since {previous}:', text)
        self.assertIn('new in 0.14, the quicker restore of the playing filter', text)
        # VST3-26 changed how the two VST3 halves keep in step after every VST3 host check.
        self.assertIn('which every VST3 check above predates', text)
        self.assertIn('https://github.com/owner/convolver/releases/tag/v0.18.12', text)
        self.assertIn('https://github.com/owner/convolver/releases/tag/v0.18.29', text)
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
        words = prepare_release.notes_text()
        self.assertIn(f'Since {words["previous"]}:', text)
        for change in words['changes']:
            self.assertIn(f'- {change}\n', text)
        # A preview says what was not tried in a host (REL-13 keeps it to previews).
        self.assertIn('nobody has used it in a desktop host yet', text)
        self.assertIn('Nobody has tried it in a Windows host or with a screen reader yet', text)
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

    def test_notes_text_is_read_as_its_page_says(self):
        # REL-13: the words come from packaging/release-notes.md; each entry's
        # wrapped lines are one line in the notes, and the markers decide
        # who reads a limitation.
        page = self.root / 'release-notes.md'
        good = (
            '# Release notes text\n\nAnything here.\n\n## Previous preview\n\n1.2.3\n\n'
            '## What changed\n\n- One change,\n  wrapped.\n- Another.\n\n'
            '## Known limitations\n\n- For all.\n- <!-- previews --> Previews,\n  only.\n'
            '- <!-- full releases --> Full only.\n- <!-- generated -->\n'
        )
        page.write_text(good)
        self.assertEqual(
            prepare_release.notes_text(page),
            {
                'previous': '1.2.3',
                'changes': ['One change, wrapped.', 'Another.'],
                'limitations': [
                    ('both', 'For all.'),
                    ('preview', 'Previews, only.'),
                    ('production', 'Full only.'),
                    ('generated', ''),
                ],
            },
        )
        for old, new, words in [
            ('1.2.3', 'next', 'must hold one version'),
            ('## What changed', '## Changes', 'no "## What changed" section'),
            ('- Another.', 'Another.', 'holds only list entries'),
            ('- <!-- generated -->\n', '', 'exactly one "- <!-- generated -->"'),
            ('- <!-- generated -->\n', '- <!-- generated --> words\n', 'stands alone'),
            ('<!-- previews -->', '<!-- beta -->', "unknown marker '<!-- beta -->'"),
            ('<!-- previews -->', '<!--previews-->', 'a comment in "## Known limitations"'),
            ('- Another.', '- Another <!-- hidden -->.', 'a comment in "## What changed"'),
            ('- For all.', '- For {doc}/all.', 'unknown placeholder {doc}'),
            (
                '## Known limitations',
                '## What changed\n\n- Again.\n\n## Known limitations',
                'a second "## What changed"',
            ),
        ]:
            with self.subTest(words=words):
                page.write_text(good.replace(old, new))
                with self.assertRaisesRegex(ValueError, re.escape(words)):
                    prepare_release.notes_text(page)

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
        name = f'Convolver-{self.version}-macos-arm64-unsigned.pkg'
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
            self.assertIn(f'Convolver-{version}-{platform}.zip: not signed', problems)
            self.assertIn(f'Convolver-{version}-{platform}.zip: not notarized', problems)
            self.assertIn(f'Convolver-{version}-{platform}-unsigned.pkg: the file name says unsigned', problems)
        # jrp, 26 November 2026: Windows ships unsigned, and Windows and Linux without a named-host test.
        self.assertFalse([p for p in problems if 'windows' in p and ('not signed' in p or 'says unsigned' in p)])
        for problem in problems:
            if 'missing, failed or stale' in problem and ('windows' in problem or 'linux' in problem):
                self.assertNotIn('host', problem)
        # jrp, 26 November 2026: no file name says preview.
        self.assertFalse([p for p in problems if 'says preview' in p])
        self.assertEqual(len(names), 8)
        self.assertEqual((self.output / 'title.txt').read_text(), f'Convolver {version}\n')
        text = (self.output / 'notes.md').read_text()
        self.assertNotIn('preview.**', text)
        self.assertNotIn('Unsigned developer preview', text)
        self.assertIn('The macOS files are signed with an Apple Developer ID and notarized.', text)
        self.assertIn('The Windows files are not signed with a code-signing certificate', text)
        self.assertIn('- The Windows files are not signed: Windows shows a SmartScreen warning', text)
        self.assertIn(
            '- Not verified in a named host: no host test was accepted for VST3 on Windows, or for VST3 and LV2 on '
            'Linux ',
            text,
        )
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
        self.assertIn(f'Convolver-{self.version}-windows-x86_64.zip: not signed', problems)
        _, problems = self.production(dry_run=True)
        self.assertNotIn(f'Convolver-{self.version}-windows-x86_64.zip: not signed', problems)

    def test_a_platform_without_requirements_is_listed(self):
        policy = json.loads((ROOT / 'packaging/release-policy.json').read_text())
        del policy['platforms'][0]['production']
        requirements = self.temp / 'requirements'
        (requirements / 'packaging').mkdir(parents=True)
        shutil.copyfile(ROOT / 'VERSION', requirements / 'VERSION')
        (requirements / 'packaging/release-policy.json').write_text(json.dumps(policy))
        _, problems = self.production(dry_run=True, requirements=requirements / 'packaging/release-policy.json')
        self.assertIn(
            f'Convolver-{self.version}-windows-x86_64.zip: the release policy gives no production rules '
            'for windows-x86_64',
            problems,
        )
        self.assertIn('Convolver ', (self.output / 'notes.md').read_text())

    def test_evidence_file_is_bound_to_the_files(self):
        name = f'Convolver-{self.version}-linux-x86_64.zip'
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

    def full_evidence(self, host=None):
        """Every required kind passed on every platform file, with host tests
        as `host` gives them per platform id (default: a covering one)."""
        candidate = json.loads((self.root / 'candidate.json').read_text())
        entries = []
        for record in candidate['platforms']:
            platform = next(p for p in self.policy['platforms'] if p['id'] == record['id'])
            files = {name: sha256(self.root / name) for name in [record['artifact'], *record['installers']]}
            for kind in self.policy['required_production_evidence']:
                entry = {'kind': kind, 'result': 'passed', 'record': f'https://example.org/{record["id"]}/{kind}'}
                if kind == 'host':
                    fields = (host or {}).get(
                        record['id'],
                        {
                            'host': 'REAPER',
                            'host_version': '7.86',
                            'os_build': 'build ' + record['id'],
                            'architecture': platform['architecture'],
                            'sample_rates': [44100, 48000, 96000],
                            'channels': [1, 2],
                        },
                    )
                    if fields is None:
                        continue
                    # REL-17: one test for every format the platform ships.
                    entry.update({'formats': platform['formats'], **fields})
                entries.append(dict(entry, files=files))
        return {'schema': 1, 'version': self.version, 'evidence': entries}

    def checked(self, evidence, unverified=None):
        """The production policy (this checkout's rules, with `unverified`
        overriding a platform's by id) and the candidate with this evidence."""
        requirements = load_policy(ROOT / 'packaging/release-policy.json')
        for platform in requirements['platforms']:
            if platform['id'] in (unverified or {}):
                platform['production'] = dict(platform['production'], unverified=unverified[platform['id']])
        policy = prepare_release.production_policy(self.policy, requirements)
        candidate = prepare_release.with_evidence(
            json.loads((self.root / 'candidate.json').read_text()), evidence, policy
        )
        return policy, candidate

    def facts(self, evidence, unverified=None):
        """verification_facts() by platform id, each file's facts by its
        kind ('zip' or 'installer')."""
        policy, candidate = self.checked(evidence, unverified)
        return {
            fact['platform']: {'zip' if f['label'] == 'ZIP archive' else 'installer': f for f in fact['files']}
            for fact in prepare_release.verification_facts(policy, candidate)
        }

    def lines(self, evidence, unverified=None):
        """The opening sentence and known limitations production_lines() makes."""
        policy, candidate = self.checked(evidence, unverified)
        return prepare_release.production_lines(policy, candidate, 'DOCS', 'BASE', 'TAG')

    def host_only(self, evidence, keep):
        """The evidence with each host test cut to the files `keep` accepts."""
        for entry in evidence['evidence']:
            if entry['kind'] == 'host':
                entry['files'] = {name: digest for name, digest in entry['files'].items() if keep(name)}
        return evidence

    @staticmethod
    def hostless(limitations):
        """The facts the named-host limitation carries: what lacks an
        accepted host test, and whether the notes say each of those files
        was checked automatically; None when there is no such limitation."""
        found = [line for line in limitations if 'named host' in line]
        if not found:
            return None
        (line,) = found
        named = re.fullmatch(
            r'- Not verified in a named host: no host test was accepted for (.+) in this release'
            r'(, and each was checked only automatically, on CI machines that had never built '
            r'Convolver)? \(\[status\]\(\S+/docs/project/status\.md\)\)\.',
            line,
        )
        assert named, line
        return named[1], bool(named[2])

    def test_notes_say_what_the_host_evidence_verified(self):
        # REL-12 (jrp's review of 7bd024f): with passing, hash-matched host
        # tests for Windows and Linux, the gate accepted them but the notes
        # still said nobody had tested those platforms in a host.
        evidence = self.full_evidence()
        facts = self.facts(evidence)
        version = self.version
        for platform, files in facts.items():
            self.assertEqual(sorted(files), ['zip'] if platform.startswith('linux') else ['installer', 'zip'])
            formats = next(p['formats'] for p in self.policy['platforms'] if p['id'] == platform)
            for file in files.values():
                self.assertEqual(
                    file['hosts'],
                    [
                        {
                            'host': 'REAPER',
                            'host_version': '7.86',
                            'os_build': 'build ' + platform,
                            'record': f'https://example.org/{platform}/host',
                            'format': format_name,
                        }
                        for format_name in formats
                    ],
                )
                self.assertEqual(file['outstanding'], [])
                self.assertEqual(file['verified'], self.policy['required_production_evidence'])
        self.assertEqual(facts['windows-x86_64']['zip']['name'], f'Convolver-{version}-windows-x86_64.zip')
        self.assertEqual(
            facts['windows-x86_64']['installer']['name'], f'Convolver-{version}-windows-x86_64-unsigned-setup.exe'
        )
        # The notes present those facts: host, version, system build and
        # architecture of each test, its record linked, nothing listed as untested.
        sentence, limitations = self.lines(evidence)
        self.assertFalse([line for line in limitations if 'Not verified' in line], limitations)
        self.assertIn(
            '[REAPER 7.86](https://example.org/windows-x86_64/host) on Windows (build windows-x86_64, x86_64) '
            'for VST3;',
            sentence,
        )
        for platform in facts:
            self.assertIn(f'(https://example.org/{platform}/host) on ', sentence)
        _, problems = self.production(dry_run=True, evidence=evidence)
        self.assertFalse([p for p in problems if 'evidence' in p], problems)
        text = (self.output / 'notes.md').read_text()
        self.assertIn(sentence[sentence.index('Tested by hand') :], text)
        self.assertNotIn('Not verified in a named host', text)
        # The complete notes, not only the opening (Codex's review of #314,
        # REL-13): no retained limitation says nobody used the Windows window
        # in a host, which a Windows host test contradicts (the VST3 plug-in,
        # whose window it is, is Windows' only format); what no evidence
        # covers stays. REL-17: a host test names its formats, so a full
        # release with an LV2 host test no longer says nobody used the LV2
        # plug-in in a desktop host; without one, the generated line names
        # LV2 on Linux (test_policy_decides_which_missing_host_tests_may_ship).
        self.assertNotIn('Nobody has tried it in a Windows host', text)
        self.assertNotIn('nobody has used it in a desktop host', text)
        self.assertIn('a test in a desktop host, when the release has one, is named above', text)
        self.assertIn('The Windows window has not been tried with a screen reader yet.', text)
        self.assertIn('It was checked automatically, with lilv', text)

    def test_policy_decides_which_missing_host_tests_may_ship(self):
        # Linux without a host test, which the policy allows: outstanding,
        # and the notes name Linux, and only Linux, as not verified in a host.
        evidence = self.full_evidence({'linux-x86_64': None})
        facts = self.facts(evidence)
        for file in facts['linux-x86_64'].values():
            self.assertEqual(file['hosts'], [])
            self.assertEqual(file['outstanding'], ['host:vst3', 'host:lv2'])
        self.assertEqual(facts['windows-x86_64']['zip']['outstanding'], [])
        sentence, limitations = self.lines(evidence)
        self.assertNotIn('on Linux', sentence)
        self.assertIn('on Windows (build windows-x86_64, x86_64) for VST3;', sentence)
        self.assertEqual(self.hostless(limitations), ('VST3 and LV2 on Linux', True))
        # A failed host test is never ignored, even where one may be missing.
        failed = self.full_evidence()
        for entry in failed['evidence']:
            if entry['kind'] == 'host' and any('linux' in name for name in entry['files']):
                entry['result'] = 'failed'
        self.assertEqual(self.facts(failed)['linux-x86_64']['zip']['outstanding'], ['host:vst3', 'host:lv2'])
        _, problems = self.production(dry_run=True, evidence=failed)
        self.assertIn(
            f'Convolver-{self.version}-linux-x86_64.zip: missing, failed or stale production evidence: '
            'host (vst3, lv2)',
            problems,
        )
        # A host test that does not cover mono and stereo verifies nothing,
        # and the gate refuses it: such notes are never published.
        short = {
            'host': 'REAPER',
            'host_version': '7.86',
            'os_build': 'b',
            'architecture': 'x86_64',
            'sample_rates': [44100, 48000, 96000],
            'channels': [2],
        }
        evidence = self.full_evidence({'windows-x86_64': short})
        self.assertEqual(self.facts(evidence)['windows-x86_64']['zip']['outstanding'], ['host:vst3'])
        _, problems = self.production(dry_run=True, evidence=evidence)
        self.assertIn(
            f'Convolver-{self.version}-windows-x86_64.zip: vst3 host evidence lacks required platform/rate/layout '
            'coverage',
            problems,
        )
        # macOS may not ship untested: never outstanding, the gate refuses it.
        evidence = self.full_evidence({'macos-arm64': None})
        facts = self.facts(evidence)
        self.assertEqual(facts['macos-arm64']['zip']['hosts'], [])
        self.assertEqual(facts['macos-arm64']['zip']['outstanding'], [])
        _, problems = self.production(dry_run=True, evidence=evidence)
        self.assertIn(
            f'Convolver-{self.version}-macos-arm64.zip: missing, failed or stale production evidence: host (vst3, au)',
            problems,
        )
        # REL-17 (pre-1.0 review of 0.19.21): a REAPER VST3 test does not
        # stand for the Audio Unit in the same macOS files.
        evidence = self.full_evidence()
        for entry in evidence['evidence']:
            if entry['kind'] == 'host' and any('macos' in name for name in entry['files']):
                entry['formats'] = ['vst3']
        _, problems = self.production(dry_run=True, evidence=evidence)
        for platform in ('macos-arm64', 'macos-x86_64'):
            self.assertIn(
                f'Convolver-{self.version}-{platform}.zip: missing, failed or stale production evidence: host (au)',
                problems,
            )
        # With a GarageBand Audio Unit test as well, the gate passes and the
        # notes name each test with its format.
        for entry in list(evidence['evidence']):
            if entry['kind'] == 'host' and any('macos' in name for name in entry['files']):
                evidence['evidence'].append(
                    dict(entry, formats=['au'], host='GarageBand', host_version='10.4', record=entry['record'] + '/au')
                )
        _, problems = self.production(dry_run=True, evidence=evidence)
        self.assertFalse([p for p in problems if 'evidence' in p], problems)
        sentence, _ = self.lines(evidence)
        self.assertIn(
            '[REAPER 7.86](https://example.org/macos-arm64/host) on macOS (build macos-arm64, arm64) for VST3; '
            '[GarageBand 10.4](https://example.org/macos-arm64/host/au) on macOS (build macos-arm64, arm64) for '
            'the Audio Unit;',
            sentence,
        )

    def test_notes_say_which_files_a_host_test_holds_for(self):
        # The independent review of #314: the gate judges each file on its
        # own evidence, so a host test given for the Windows ZIP alone is
        # accepted, and the notes must neither say nobody tested Windows in a
        # host nor claim the test for the installer.
        evidence = self.host_only(self.full_evidence(), lambda name: 'windows' not in name or name.endswith('.zip'))
        facts = self.facts(evidence)['windows-x86_64']
        self.assertTrue(facts['zip']['hosts'])
        self.assertEqual(facts['zip']['outstanding'], [])
        self.assertEqual(facts['installer']['hosts'], [])
        self.assertEqual(facts['installer']['outstanding'], ['host:vst3'])
        _, problems = self.production(dry_run=True, evidence=evidence)
        self.assertFalse([p for p in problems if 'evidence' in p], problems)
        sentence, limitations = self.lines(evidence)
        self.assertIn(
            '[REAPER 7.86](https://example.org/windows-x86_64/host) on Windows (build windows-x86_64, x86_64) for '
            'VST3, with the ZIP archive;',
            sentence,
        )
        self.assertEqual(self.hostless(limitations), ('VST3 on the Windows (x86_64) installer', True))
        # Two different host tests for the two files: each named, with its record.
        evidence = self.full_evidence()
        for entry in list(evidence['evidence']):
            if entry['kind'] == 'host' and any('windows' in name for name in entry['files']):
                installer = {name: digest for name, digest in entry['files'].items() if name.endswith('.exe')}
                entry['files'] = {name: digest for name, digest in entry['files'].items() if name.endswith('.zip')}
                evidence['evidence'].append(
                    dict(
                        entry,
                        files=installer,
                        host='Bitwig Studio',
                        host_version='6.0',
                        os_build='other',
                        record='https://example.org/bitwig',
                    )
                )
        facts = self.facts(evidence)['windows-x86_64']
        self.assertEqual(facts['installer']['hosts'][0]['record'], 'https://example.org/bitwig')
        self.assertEqual(facts['zip']['hosts'][0]['record'], 'https://example.org/windows-x86_64/host')
        sentence, limitations = self.lines(evidence)
        self.assertIn('on Windows (build windows-x86_64, x86_64) for VST3, with the ZIP archive;', sentence)
        self.assertIn(
            '[Bitwig Studio 6.0](https://example.org/bitwig) on Windows (other, x86_64) for VST3, with the installer;',
            sentence,
        )
        self.assertIsNone(self.hostless(limitations))

    def test_notes_name_the_architecture_only_part_of_a_system_lacks(self):
        # Rules that let both Macs ship without a host test and an upgrade
        # test, with only the Intel Mac lacking the host test: the host
        # limitation names its architecture, the upgrade one macOS alone.
        evidence = self.full_evidence({'macos-x86_64': None})
        for entry in list(evidence['evidence']):
            if entry['kind'] == 'upgrade' and any('macos' in name for name in entry['files']):
                evidence['evidence'].remove(entry)
        unverified = {'macos-arm64': ['host', 'upgrade'], 'macos-x86_64': ['host', 'upgrade']}
        facts = self.facts(evidence, unverified)
        self.assertEqual(facts['macos-arm64']['zip']['outstanding'], ['upgrade'])
        self.assertEqual(facts['macos-x86_64']['zip']['outstanding'], ['host:vst3', 'host:au', 'upgrade'])
        _, limitations = self.lines(evidence, unverified)
        self.assertEqual(self.hostless(limitations), ('VST3 and the Audio Unit on macOS (x86_64)', True))
        self.assertIn('- Not verified on macOS: upgrade.', limitations)
        # Without an accepted automatic check of those files, the notes do not claim one.
        for entry in evidence['evidence']:
            if entry['kind'] == 'clean_install' and any('macos-x86_64' in name for name in entry['files']):
                entry['result'] = 'failed'
        _, limitations = self.lines(evidence, unverified)
        self.assertEqual(self.hostless(limitations), ('VST3 and the Audio Unit on macOS (x86_64)', False))


class VerificationFactsTests(unittest.TestCase):
    """verification_facts() and the notes on candidates the fixture cannot
    build: several installers on one platform, a platform missing."""

    def entry(self, digest, host=None):
        entry = {'result': 'passed', 'artifact_sha256': digest, 'record': f'https://example.org/{digest}'}
        if host:
            entry.update(
                record=f'https://example.org/{host}',
                host=host,
                host_version='1',
                os_build='b',
                architecture='x86_64',
                sample_rates=[44100, 48000, 96000],
                channels=[1, 2],
            )
        return entry

    def evidence(self, digest, host):
        kinds = {'automated': self.entry(digest), 'clean_install': self.entry(digest)}
        if host:
            kinds['host'] = {'vst3': self.entry(digest, host)}
        return {'evidence': kinds}

    def lines(self, hosts):
        """Windows x86-64 with a ZIP and installers `a.exe` and `b.exe`, the
        host test of each file as `hosts` names it, and Windows on Arm,
        first in the policy, missing from the candidate."""
        rules = {'signed': False, 'unverified': ['host']}
        policy = {
            'required_production_evidence': ['automated', 'clean_install', 'host'],
            'platforms': [
                {
                    'id': 'windows-arm64',
                    'os': 'Windows',
                    'architecture': 'arm64',
                    'formats': ['vst3'],
                    'production': rules,
                },
                {
                    'id': 'windows-x86_64',
                    'os': 'Windows',
                    'architecture': 'x86_64',
                    'formats': ['vst3'],
                    'production': rules,
                },
            ],
        }
        names = ['w.zip', 'a.exe', 'b.exe']
        record = {'id': 'windows-x86_64', 'artifact': 'w.zip', 'installers': ['a.exe', 'b.exe']}
        record.update(self.evidence('w.zip', hosts.get('w.zip')))
        record['installer_evidence'] = {name: self.evidence(name, hosts.get(name)) for name in names[1:]}
        candidate = {'assets': [{'name': name, 'sha256': name} for name in names], 'platforms': [record]}
        facts = prepare_release.verification_facts(policy, candidate)
        sentence, limitations = prepare_release.production_lines(policy, candidate, 'DOCS', 'BASE', 'TAG')
        return facts, sentence, ProductionTests.hostless(limitations)

    def test_several_installers_are_named_by_file(self):
        facts, sentence, hostless = self.lines({'w.zip': 'REAPER', 'a.exe': 'REAPER'})
        self.assertEqual(
            [f['label'] for f in facts[1]['files']], ['ZIP archive', 'installer `a.exe`', 'installer `b.exe`']
        )
        self.assertIn('on Windows (b, x86_64) for VST3, with the ZIP archive and installer `a.exe`.', sentence)
        self.assertEqual(hostless, ('VST3 on the Windows (x86_64) installer `b.exe`', True))
        # No file is named both as tested and as lacking a host test.
        for file in facts[1]['files']:
            self.assertNotEqual(file['label'] in sentence, file['label'] in hostless[0], file)

    def test_a_platform_missing_from_the_candidate_hides_nothing(self):
        facts, sentence, hostless = self.lines({})
        self.assertEqual(facts[0]['files'], [])
        self.assertNotIn('Tested by hand', sentence)
        # Every Windows file this release has lacks one: Windows is named.
        self.assertEqual(hostless, ('VST3 on Windows', True))


if __name__ == '__main__':
    unittest.main()
