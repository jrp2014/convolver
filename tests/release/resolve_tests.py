# SPDX-License-Identifier: GPL-2.0-or-later
"""Builds resolve each dependency to its newest upstream release and record it exactly (CI-6)."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import contract  # noqa: E402  pylint: disable=wrong-import-position
import resolve  # noqa: E402  pylint: disable=wrong-import-position
import upstream  # noqa: E402  pylint: disable=wrong-import-position


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


def fake_fetcher(url, target):
    """Each URL's 'archive' is its own text, so the checksum depends on the URL."""
    Path(target).write_bytes(url.encode())


def digest(url):
    return hashlib.sha256(url.encode()).hexdigest()


# Upstream tags as each repository might have them, newest last.
TAGS = {
    'https://github.com/steinbergmedia/vst3sdk': {'v3.8.0_build_66': '1' * 40, 'v3.8.1_build_84': '2' * 40,
                                                  'vstsdk370_31_07_2020_build_116': '3' * 40},
    'https://github.com/lv2/lv2': {'v1.18.10': ('4' * 40, '5' * 40), 'v1.20.0-rc1': '6' * 40},
    'https://github.com/apple/AudioUnitSDK': {'AudioUnitSDK-1.3.0': '7' * 40, 'AudioUnitSDK-1.4.0': '8' * 40},
    'https://github.com/FFTW/fftw3': {'fftw-3.3.11-rc5': '9' * 40, 'fftw-3.3.11': 'a' * 40},
    'https://github.com/libsndfile/libsndfile': {'1.2.2': 'b' * 40, 'v1.0.30': 'c' * 40},
    'https://github.com/kichik/nsis': {'v311': 'd' * 40, 'v313': 'e' * 40, 'v3061': 'f' * 40, 'v2.46': '0' * 40},
    'https://github.com/Tracktion/pluginval': {'v1.0.4': '1' * 40, 'v2.0.0-beta3': '2' * 40},
}
SUBMODULES = {'base': 'b' * 39 + '1', 'cmake': 'b' * 39 + '2', 'pluginterfaces': 'b' * 39 + '3',
              'public.sdk': 'b' * 39 + '4'}
URLS = {'base': '../vst3_base', 'cmake': '../vst3_cmake.git', 'pluginterfaces': '../vst3_pluginterfaces',
        'public.sdk': 'https://github.com/steinbergmedia/vst3_public_sdk', 'doc': '../vst3_doc'}


def lister(remote):
    return listing(TAGS[remote])


def links(remote, tag, paths):
    assert remote == 'https://github.com/steinbergmedia/vst3sdk' and tag == 'v3.8.1_build_84', (remote, tag)
    return {path: SUBMODULES[path] for path in paths}, URLS


class VersionTests(unittest.TestCase):
    def test_version_order(self):
        ordered = ['1.0.29', '1.1.0beta2', '1.1.0', '1.2.2', '2.0.0-beta1', '2.0.0-beta3', '2.0.0']
        self.assertEqual(sorted(reversed(ordered), key=upstream.version_key), ordered)
        self.assertLess(upstream.version_key('3.8.0_build_66'), upstream.version_key('3.8.1_build_84'))
        self.assertLess(upstream.version_key('3.8.1_build_84'), upstream.version_key('3.8.1_build_90'))
        self.assertIsNone(upstream.version_key('vstsdk370_31_07_2020_build_116'))
        fftw = ['3.3.11-rc5', '3.3.11', '3.3.11-pl1', '3.3.12']
        self.assertEqual(sorted(reversed(fftw), key=upstream.version_key), fftw)
        self.assertTrue(upstream.is_final('3.3.11-pl1'))
        self.assertLess(upstream.version_key('3.06.1'), upstream.version_key('3.13'))

    def test_annotated_tags_resolve_to_commits(self):
        tags = upstream.parse_ls_remote(listing({'v1.0': ('a' * 40, 'b' * 40), 'v1.1': 'c' * 40}))
        self.assertEqual(tags, {'v1.0': 'b' * 40, 'v1.1': 'c' * 40})

    def test_newest_final_release(self):
        rule = {'upstream': 'x', 'tag_prefix': 'v'}
        tags = {'v1.18.10': 'a' * 40, 'v1.18.12': 'b' * 40, 'v1.20.0-rc1': 'c' * 40}
        self.assertEqual(upstream.newest(rule, tags), ('1.18.12', 'v1.18.12', 'b' * 40))

    def test_pre_releases_only_where_the_rule_allows(self):
        tags = {'v2.0.0-beta3': 'a' * 40, 'v2.0.0-beta4': 'b' * 40, 'v1.0.4': 'c' * 40}
        self.assertEqual(upstream.newest({'upstream': 'x', 'tag_prefix': 'v'}, tags)[0], '1.0.4')
        self.assertEqual(upstream.newest({'upstream': 'x', 'tag_prefix': 'v', 'prereleases': True}, tags)[0],
                         '2.0.0-beta4')
        tags['v2.0.0'] = 'd' * 40
        self.assertEqual(upstream.newest({'upstream': 'x', 'tag_prefix': 'v', 'prereleases': True}, tags)[0],
                         '2.0.0')

    def test_tag_pattern(self):
        rule = {'upstream': 'x', 'tag_pattern': r'v(3)(\d\d)(\d?)'}
        tags = {tag: commit for tag, commit in TAGS['https://github.com/kichik/nsis'].items()}
        self.assertEqual(upstream.newest(rule, tags), ('3.13', 'v313', 'e' * 40))
        self.assertEqual(upstream.tag_version(rule, 'v3061'), '3.06.1')
        self.assertIsNone(upstream.tag_version(rule, 'v2.46'))

    def test_requested_and_missing_releases(self):
        rule = {'upstream': 'https://example.org/sdk', 'tag_prefix': 'v'}
        self.assertEqual(upstream.newest(rule, {'v1.0': 'a' * 40, 'v1.1': 'b' * 40}, '1.0')[0], '1.0')
        with self.assertRaisesRegex(ValueError, 'no release 9.9 among the tags of https://example.org/sdk'):
            upstream.newest(rule, {'v1.0': 'a' * 40}, '9.9')
        with self.assertRaisesRegex(ValueError, 'no release tags found at https://example.org/sdk'):
            upstream.newest(rule, {'release-1': 'a' * 40})

    def test_submodule_urls(self):
        parent = 'https://github.com/steinbergmedia/vst3sdk'
        self.assertEqual(resolve.submodule_url(parent, '../vst3_base'), 'https://github.com/steinbergmedia/vst3_base')
        self.assertTrue(upstream.same_repository('https://github.com/a/b.git', 'https://github.com/A/b/'))


class ResolveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = contract.load_policy(ROOT / 'packaging/release-policy.json')

    def resolve(self, names=None, wanted=None, policy=None, **kwargs):
        with tempfile.TemporaryDirectory() as temporary:
            return resolve.resolve(policy or self.policy, Path(temporary), names, wanted,
                                   lister=kwargs.get('lister', lister), links=kwargs.get('links', links),
                                   fetcher=fake_fetcher)

    def test_every_dependency_resolves_to_its_newest_release(self):
        records = self.resolve()
        self.assertEqual(set(records), contract.REQUIRED_DEPENDENCIES)
        self.assertEqual((records['lv2']['version'], records['lv2']['revision']), ('1.18.10', '5' * 40))
        self.assertEqual(records['audiounitsdk']['version'], '1.4.0')
        self.assertEqual(records['fftw']['url'], 'https://www.fftw.org/fftw-3.3.11.tar.gz')
        self.assertEqual(records['vst3sdk']['url'],
                         'https://codeload.github.com/steinbergmedia/vst3sdk/tar.gz/' + '2' * 40)
        for name, path in (('vst3-base', 'base'), ('vst3-public-sdk', 'public.sdk')):
            self.assertEqual(records[name]['revision'], SUBMODULES[path])
            self.assertEqual(records[name]['version'], '3.8.1_build_84')
            self.assertIn('from vst3sdk v3.8.1_build_84', records[name]['chosen'])
        for name, record in records.items():
            self.assertEqual(record['sha256'], digest(record['url']), name)
            self.assertEqual(record['license'], self.policy['dependencies'][name]['license'])
        contract.validate_records(records)
        self.assertEqual(set(contract.with_records(self.policy, records)['dependencies']), set(records))

    def test_requested_version(self):
        records = self.resolve({'audiounitsdk'}, {'audiounitsdk': '1.3.0'})
        self.assertEqual(set(records), {'audiounitsdk'})
        self.assertEqual((records['audiounitsdk']['version'], records['audiounitsdk']['chosen']),
                         ('1.3.0', 'requested (1.3.0)'))

    def test_a_submodule_brings_its_sdk(self):
        self.assertEqual(set(self.resolve({'vst3-base'})), {'vst3sdk', 'vst3-base'})

    def test_every_failure_is_reported_together(self):
        def failing(remote):
            if 'AudioUnitSDK' in remote or 'lv2' in remote:
                raise RuntimeError('host unreachable')
            return lister(remote)
        with self.assertRaises(resolve.ResolveError) as raised:
            self.resolve({'lv2', 'audiounitsdk', 'libsndfile'}, lister=failing)
        message = str(raised.exception)
        self.assertIn('audiounitsdk: host unreachable', message)
        self.assertIn('lv2: host unreachable', message)
        self.assertNotIn('libsndfile', message)
        self.assertIn('docs/develop/build.md#supporting-libraries', message)

    def test_a_changed_sdk_layout_is_explained(self):
        def moved(remote, tag, paths):
            commits, urls = links(remote, tag, paths)
            del commits['cmake']
            return commits, dict(urls, base='../somewhere_else')
        with self.assertRaises(resolve.ResolveError) as raised:
            self.resolve({'vst3-base', 'vst3-cmake'}, links=moved)
        self.assertIn('vst3-cmake: vst3sdk v3.8.1_build_84 has no submodule at cmake', str(raised.exception))
        self.assertIn('takes base from https://github.com/steinbergmedia/somewhere_else', str(raised.exception))

    def test_unknown_names(self):
        with self.assertRaisesRegex(resolve.ResolveError, 'unknown dependencies'):
            self.resolve({'clap'})
        with self.assertRaisesRegex(resolve.ResolveError, 'not being resolved'):
            self.resolve({'lv2'}, {'fftw': '3.3.10'})

    def test_lock_round_trip_and_markdown(self):
        records = self.resolve({'lv2'})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'lock.json'
            path.write_text(json.dumps(resolve.lock(records, '2026-10-24T00:00:00Z')))
            self.assertEqual(resolve.load_records(path), records)
            path.write_text(json.dumps({'schema': 1}))
            with self.assertRaisesRegex(ValueError, 'no dependency records'):
                resolve.load_records(path)
        table = resolve.markdown(records)
        self.assertIn('| [lv2](https://codeload.github.com/lv2/lv2/tar.gz/' + '5' * 40 + ') | 1.18.10 | `v1.18.10` |',
                      table)

    def test_cached_archive_reused_only_for_the_same_url(self):
        calls = []

        def counting(url, target):
            calls.append(url)
            fake_fetcher(url, target)
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            resolve.fetch_archive('https://example.org/a', cache, 'x.tar.gz', counting)
            resolve.fetch_archive('https://example.org/a', cache, 'x.tar.gz', counting)
            path = resolve.fetch_archive('https://example.org/b', cache, 'x.tar.gz', counting)
            self.assertEqual(calls, ['https://example.org/a', 'https://example.org/b'])
            self.assertEqual(path.read_bytes(), b'https://example.org/b')


class ToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = contract.load_policy(ROOT / 'packaging/release-policy.json')

    def tool(self, name, **kwargs):
        temporary = tempfile.TemporaryDirectory()  # pylint: disable=consider-using-with
        self.addCleanup(temporary.cleanup)
        return resolve.resolve_tool(self.policy, name, Path(temporary.name), kwargs.pop('os_name', None),
                                    kwargs.pop('wanted', None), lister=kwargs.pop('lister', lister),
                                    fetcher=kwargs.pop('fetcher', fake_fetcher),
                                    digest=kwargs.pop('digest', lambda *args: None))

    def test_nsis(self):
        record, path = self.tool('nsis')
        self.assertEqual((record['version'], record['tag']), ('3.13', 'v313'))
        self.assertEqual(record['url'], 'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.13/nsis-3.13.zip')
        self.assertEqual(path.name, 'nsis-3.13.zip')
        self.assertEqual(self.tool('nsis', wanted='3.11')[0]['version'], '3.11')

    def test_pluginval_per_os_with_published_digest(self):
        asset_url = 'https://github.com/Tracktion/pluginval/releases/download/v2.0.0-beta3/pluginval_macOS.zip'
        record, _ = self.tool('pluginval', os_name='macOS', digest=lambda *args: digest(asset_url))
        self.assertEqual((record['version'], record['url']), ('2.0.0-beta3', asset_url))
        self.assertEqual(record['checked_against'], 'the digest GitHub publishes for the asset')
        with self.assertRaisesRegex(resolve.ResolveError, 'differs from the digest GitHub publishes'):
            self.tool('pluginval', os_name='Linux', digest=lambda *args: '0' * 64)
        with self.assertRaisesRegex(resolve.ResolveError, "no download for 'Plan9'"):
            self.tool('pluginval', os_name='Plan9')

    def test_lv2lint_falls_back_to_freebsd_ports(self):
        distinfo = ('TIMESTAMP = 1\nSHA256 (lv2lint-0.16.2.tar.xz) = '
                    + digest('https://git.open-music-kontrollers.ch/~hp/lv2lint/snapshot/lv2lint-0.16.2.tar.xz')
                    + '\nSIZE (lv2lint-0.16.2.tar.xz) = 51984\n')

        def fetcher(url, target):
            if url.endswith('/distinfo'):
                Path(target).write_text(distinfo)
            else:
                fake_fetcher(url, target)

        def unreachable(remote):
            raise RuntimeError('CONNECT tunnel failed')
        record, _ = self.tool('lv2lint', lister=unreachable, fetcher=fetcher)
        self.assertEqual(record['version'], '0.16.2')
        self.assertEqual(record['checked_against'], 'FreeBSD ports distinfo')
        self.assertIn('did not answer (CONNECT tunnel failed)', record['chosen'])

    def test_unknown_tool(self):
        with self.assertRaisesRegex(resolve.ResolveError, 'unknown tool'):
            self.tool('clap-validator')


class PolicyTests(unittest.TestCase):
    def test_the_policy_pins_nothing(self):
        policy = contract.load_policy(ROOT / 'packaging/release-policy.json')
        for name in ('lv2', 'nsis'):
            broken = copy.deepcopy(policy)
            section = broken['dependencies'] if name in broken['dependencies'] else broken['release_tools']
            section[name]['sha256'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'does not pin a sha256'):
                contract.validate_policy(broken)

    def test_failure_explanation_names_the_suspect(self):
        text = resolve.failure_explanation('audiounitsdk')
        self.assertIn('a new audiounitsdk release is then the likely cause', text)
        self.assertIn('--version audiounitsdk=OLD_VERSION', text)

    def test_packages_table(self):
        table = resolve.packages_markdown(['b', 'a'], lambda names: 'liblilv-0-0\t0.24.22-1build1\nlv2-dev\t1.18.10\n')
        self.assertIn('| liblilv-0-0 | 0.24.22-1build1 |', table)


if __name__ == '__main__':
    unittest.main()
