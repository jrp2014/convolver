# SPDX-License-Identifier: GPL-2.0-or-later
"""The dependency update check reads every pin and orders upstream versions correctly."""
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import check_updates  # noqa: E402  pylint: disable=wrong-import-position


def listing(tags):
    """git ls-remote output for {tag: commit}; a tuple value is (tag object, peeled commit)."""
    lines = []
    for tag, commit in tags.items():
        if isinstance(commit, tuple):
            lines.append(f'{commit[0]}\trefs/tags/{tag}')
            lines.append(f'{commit[1]}\trefs/tags/{tag}^{{}}')
        else:
            lines.append(f'{commit}\trefs/tags/{tag}')
    return '\n'.join(lines) + '\n'


class UpdateTests(unittest.TestCase):
    def test_version_order(self):
        ordered = ['1.0.29', '1.1.0beta2', '1.1.0', '1.2.2', '2.0.0-beta1', '2.0.0-beta3', '2.0.0']
        self.assertEqual(sorted(reversed(ordered), key=check_updates.version_key), ordered)
        self.assertLess(check_updates.version_key('3.8.0_build_66'),
                        check_updates.version_key('3.8.1_build_84'))
        self.assertLess(check_updates.version_key('3.8.1_build_84'),
                        check_updates.version_key('3.8.1_build_90'))
        self.assertIsNone(check_updates.version_key('vstsdk370_31_07_2020_build_116'))
        fftw = ['3.3.11-rc5', '3.3.11', '3.3.11-pl1', '3.3.12']
        self.assertEqual(sorted(reversed(fftw), key=check_updates.version_key), fftw)
        self.assertTrue(check_updates.is_final('3.3.11-pl1'))

    def test_annotated_tags_resolve_to_commits(self):
        tags = check_updates.parse_ls_remote(listing({'v1.0': ('a' * 40, 'b' * 40), 'v1.1': 'c' * 40}))
        self.assertEqual(tags, {'v1.0': 'b' * 40, 'v1.1': 'c' * 40})

    def test_newer_final_release(self):
        pin = check_updates.Pin('lv2', '', 'v', 'policy', '1.18.10', 'a' * 40)
        row = check_updates.check(pin, {'v1.18.10': 'a' * 40, 'v1.18.12': 'b' * 40, 'v1.20.0-rc1': 'c' * 40})
        self.assertEqual((row.newest, row.update, row.problem), ('1.18.12', True, False))

    def test_pre_releases_count_only_for_a_pre_release_pin(self):
        tags = {'v2.0.0-beta3': 'a' * 40, 'v2.0.0-beta4': 'b' * 40, 'v1.0.4': 'c' * 40}
        row = check_updates.check(check_updates.Pin('pluginval', '', 'v', 'ci', '2.0.0-beta3'), tags)
        self.assertEqual((row.newest, row.update), ('2.0.0-beta4', True))
        row = check_updates.check(check_updates.Pin('x', '', 'v', 'ci', '1.0.4'), tags)
        self.assertEqual((row.newest, row.update), ('1.0.4', False))

    def test_moved_or_missing_tag_needs_attention(self):
        pin = check_updates.Pin('sdk', '', 'v', 'policy', '1.0', 'a' * 40)
        self.assertTrue(check_updates.check(pin, {'v1.0': 'b' * 40}).problem)
        self.assertTrue(check_updates.check(pin, {'v1.1': 'a' * 40}).problem)
        row = check_updates.check(pin, {'v1.0': 'b' * 40, 'v1.2': 'c' * 40})
        self.assertTrue(row.update and row.problem)
        self.assertIn('newer release; tag v1.0 now points at', row.status)

    def test_information_only_pin_does_not_ask_for_an_update(self):
        pin = check_updates.Pin('lilv', '', 'v', 'ci', '0.24.22', actionable=False)
        row = check_updates.check(pin, {'v0.24.22': 'a' * 40, 'v0.28.0': 'b' * 40})
        self.assertEqual((row.newest, row.update, row.problem), ('0.28.0', False, False))

    def test_reads_every_pin_from_the_repository(self):
        pins, checkout = check_updates.pins()
        names = {pin.name.split(' ')[0] for pin in pins}
        self.assertEqual(names, {'VST3', 'lv2', 'AudioUnitSDK', 'FFTW', 'libsndfile', 'pluginval',
                                 'lv2lint', 'lilv'})
        self.assertTrue(all(check_updates.version_key(pin.version) for pin in pins))
        self.assertEqual(checkout, pins[0].revision)

    def test_report_and_lookup_failure(self):
        def lister(remote):
            if 'lv2lint' in remote:
                raise RuntimeError('unreachable')
            return listing({})

        rows = check_updates.run(lister)
        report = check_updates.markdown(rows)
        self.assertIn('lookup failed: unreachable', report)
        self.assertIn('1 could not be checked this time', report)
        lv2lint = next(row for row in rows if row.pin.name == 'lv2lint')
        self.assertTrue(lv2lint.unreachable and not lv2lint.problem)
        others = [row for row in rows if row.pin.actionable and row.pin.name != 'lv2lint']
        self.assertTrue(all(row.problem for row in others))

    def test_workflow_checkout_must_match_policy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for path in ('packaging/release-policy.json', '.github/workflows/vst3.yml',
                         '.github/workflows/lv2.yml'):
                (root / path).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(ROOT / path, root / path)
            workflow = root / '.github/workflows/vst3.yml'
            pins, checkout = check_updates.pins(root)
            workflow.write_text(workflow.read_text(encoding='utf-8').replace(checkout, 'f' * 40),
                                encoding='utf-8')
            tags = {}
            for pin in pins:
                if pin.revision:
                    tags[pin.tag_prefix + pin.version] = pin.revision
            rows = check_updates.run(lambda remote: listing(tags), root)
            self.assertIn('VST3 workflow checkout', [row.pin.name for row in rows if row.problem])


if __name__ == '__main__':
    unittest.main()
