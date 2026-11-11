# SPDX-License-Identifier: GPL-2.0-or-later
"""Preparing a draft pre-release must refuse any run, tag or file that is not
the candidate it claims to be (REL-8), and attach only the published files:
every platform's packages and the source (REL-9)."""

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


class PrepareReleaseTests(unittest.TestCase):
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
        shutil.copyfile(ROOT / 'packaging/release-policy.json', self.tagged / 'packaging/release-policy.json')
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
        return prepare_release.prepare(**arguments)

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
        # The fix is untested in Audacity, so the notes say so (jrp, 10 November 2026).
        self.assertIn('not yet in Audacity', text)
        self.assertIn("Audacity's own preset menu is the route that has been checked", text)
        # The new engine was published without a host listening session (jrp, 9 November 2026),
        # and some older projects open at the new default Latency (PERF-2).
        self.assertIn('nobody has listened to it in a host yet', text)
        self.assertIn('Some projects saved with 0.12.12', text)
        self.assertIn(f'https://github.com/owner/convolver/blob/{self.tag}/docs/use/plugins.md#settings', text)
        self.assertIn(f'https://github.com/owner/convolver/blob/{self.tag}/CHANGELOG.md', text)
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


if __name__ == '__main__':
    unittest.main()
