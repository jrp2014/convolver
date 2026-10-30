# SPDX-License-Identifier: GPL-2.0-or-later
"""Builds resolve each dependency to its newest upstream release and record it exactly (CI-6)."""

import contextlib
import copy
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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
    'https://github.com/steinbergmedia/vst3sdk': {
        'v3.8.0_build_66': '1' * 40,
        'v3.8.1_build_84': '2' * 40,
        'vstsdk370_31_07_2020_build_116': '3' * 40,
    },
    'https://github.com/lv2/lv2': {'v1.18.10': ('4' * 40, '5' * 40), 'v1.20.0-rc1': '6' * 40},
    'https://github.com/apple/AudioUnitSDK': {'AudioUnitSDK-1.3.0': '7' * 40, 'AudioUnitSDK-1.4.0': '8' * 40},
    'https://github.com/FFTW/fftw3': {'fftw-3.3.11-rc5': '9' * 40, 'fftw-3.3.11': 'a' * 40},
    'https://github.com/libsndfile/libsndfile': {'1.2.2': 'b' * 40, 'v1.0.30': 'c' * 40},
    'https://github.com/kichik/nsis': {'v311': 'd' * 40, 'v313': 'e' * 40, 'v3061': 'f' * 40, 'v2.46': '0' * 40},
    'https://github.com/Tracktion/pluginval': {'v1.0.4': '1' * 40, 'v2.0.0-beta3': '2' * 40},
    'https://gitlab.com/drobilla/zix.git': {'v0.8.2': '3' * 40},
    'https://gitlab.com/drobilla/serd.git': {'v0.32.10': '4' * 40},
    'https://gitlab.com/drobilla/sord.git': {'v0.16.22': '5' * 40},
    'https://gitlab.com/lv2/sratom.git': {'v0.6.22': '6' * 40},
    'https://gitlab.com/lv2/lilv.git': {'v0.26.4': '7' * 40, 'v0.28.0': '8' * 40, 'v0.30.0-rc1': '9' * 40},
}
SUBMODULES = {
    'base': 'b' * 39 + '1',
    'cmake': 'b' * 39 + '2',
    'pluginterfaces': 'b' * 39 + '3',
    'public.sdk': 'b' * 39 + '4',
}
URLS = {
    'base': '../vst3_base',
    'cmake': '../vst3_cmake.git',
    'pluginterfaces': '../vst3_pluginterfaces',
    'public.sdk': 'https://github.com/steinbergmedia/vst3_public_sdk',
    'doc': '../vst3_doc',
}


def lister(remote):
    return listing(TAGS[remote])


def links(remote, tag, paths):
    assert remote == 'https://github.com/steinbergmedia/vst3sdk' and tag == 'v3.8.1_build_84', (remote, tag)
    return {path: SUBMODULES[path] for path in paths}, URLS


class VersionTests(unittest.TestCase):
    def assert_version_order(self, ordered):
        # Sort a reversed copy, so that a key that ties versions fails.
        backwards = ordered[::-1]
        self.assertEqual(sorted(backwards, key=upstream.version_key), ordered)

    def test_version_order(self):
        ordered = ['1.0.29', '1.1.0beta2', '1.1.0', '1.2.2', '2.0.0-beta1', '2.0.0-beta3', '2.0.0']
        self.assert_version_order(ordered)
        self.assertLess(upstream.version_key('3.8.0_build_66'), upstream.version_key('3.8.1_build_84'))
        self.assertLess(upstream.version_key('3.8.1_build_84'), upstream.version_key('3.8.1_build_90'))
        self.assertIsNone(upstream.version_key('vstsdk370_31_07_2020_build_116'))
        fftw = ['3.3.11-rc5', '3.3.11', '3.3.11-pl1', '3.3.12']
        self.assert_version_order(fftw)
        self.assertTrue(upstream.is_final('3.3.11-pl1'))
        self.assertLess(upstream.version_key('3.06.1'), upstream.version_key('3.13'))

    def test_prerelease_stages(self):
        # The 0.5.2 review: beta3 outranked rc1, so CI kept the older beta.
        ordered = [
            '2.0.0-alpha2',
            '2.0.0-alpha10',
            '2.0.0-beta1',
            '2.0.0-beta3',
            '2.0.0-rc1',
            '2.0.0-pre2',
            '2.0.0-rc3',
            '2.0.0',
            '2.0.0-pl1',
            '2.0.1-alpha1',
        ]
        self.assert_version_order(ordered)
        self.assertEqual(
            sorted(['1.1.0rc1', '1.1.0beta2', '1.1.0alpha3'], key=upstream.version_key),
            ['1.1.0alpha3', '1.1.0beta2', '1.1.0rc1'],
        )
        for version in ('2.0.0-alpha1', '2.0.0-beta3', '2.0.0-rc1', '2.0.0-pre1'):
            self.assertFalse(upstream.is_final(version), version)
        for version in ('2.0.0', '2.0', '3.3.11-pl1', '3.8.1_build_84'):
            self.assertTrue(upstream.is_final(version), version)
        rule = {'upstream': 'x', 'tag_prefix': 'v'}
        tags = {'v1.0.4': 'a' * 40, 'v2.0.0-beta3': 'b' * 40, 'v2.0.0-rc1': 'c' * 40, 'v2.0.0-alpha9': 'd' * 40}
        self.assertEqual(upstream.newest_prerelease(rule, tags), ('2.0.0-rc1', 'v2.0.0-rc1', 'c' * 40))
        self.assertEqual(upstream.newest(dict(rule, prereleases=True), tags)[0], '2.0.0-rc1')
        self.assertEqual(upstream.newest(rule, tags)[0], '1.0.4')
        tags['v2.0.0'] = 'e' * 40
        self.assertIsNone(upstream.newest_prerelease(rule, tags))
        self.assertEqual(upstream.newest(dict(rule, prereleases=True), tags)[0], '2.0.0')

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
        self.assertEqual(
            upstream.newest({'upstream': 'x', 'tag_prefix': 'v', 'prereleases': True}, tags)[0], '2.0.0-beta4'
        )
        tags['v2.0.0'] = 'd' * 40
        self.assertEqual(upstream.newest({'upstream': 'x', 'tag_prefix': 'v', 'prereleases': True}, tags)[0], '2.0.0')

    def test_tag_pattern(self):
        rule = {'upstream': 'x', 'tag_pattern': r'v(3)(\d\d)(\d?)'}
        tags = dict(TAGS['https://github.com/kichik/nsis'].items())
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
            return resolve.resolve(
                policy or self.policy,
                Path(temporary),
                names,
                wanted,
                lister=kwargs.get('lister', lister),
                links=kwargs.get('links', links),
                fetcher=fake_fetcher,
                digest=kwargs.get('digest', lambda *_: None),
            )

    def test_every_dependency_resolves_to_its_newest_release(self):
        records = self.resolve()
        self.assertEqual(set(records), contract.REQUIRED_DEPENDENCIES)
        self.assertEqual((records['lv2']['version'], records['lv2']['revision']), ('1.18.10', '5' * 40))
        self.assertEqual(records['audiounitsdk']['version'], '1.4.0')
        self.assertEqual(records['lv2']['newer_prerelease'], 'v1.20.0-rc1')
        self.assertNotIn('newer_prerelease', records['audiounitsdk'])
        self.assertIn('- lv2: `v1.20.0-rc1` (not used; builds and formal tests use 1.18.10)', resolve.markdown(records))
        self.assertEqual(records['fftw']['url'], 'https://www.fftw.org/fftw-3.3.11.tar.gz')
        self.assertEqual(
            records['vst3sdk']['url'], 'https://codeload.github.com/steinbergmedia/vst3sdk/tar.gz/' + '2' * 40
        )
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
        self.assertEqual(
            (records['audiounitsdk']['version'], records['audiounitsdk']['chosen']), ('1.3.0', 'requested (1.3.0)')
        )

    def test_prerelease_notes_warn_but_stay_out_of_records(self):
        records = self.resolve({'lv2', 'audiounitsdk'})
        with mock.patch.dict('os.environ', {'GITHUB_ACTIONS': 'true'}), contextlib.redirect_stdout(
            io.StringIO()
        ) as printed:
            resolve.warn_prereleases(records)
        self.assertEqual(printed.getvalue().count('::warning'), 1)
        self.assertIn('lv2 v1.20.0-rc1 is out upstream; builds and formal tests use 1.18.10', printed.getvalue())
        self.assertNotIn('newer_prerelease', resolve.lock(records)['dependencies']['lv2'])
        policy = dict(self.policy, dependencies={n: self.policy['dependencies'][n] for n in records})
        self.assertNotIn('newer_prerelease', contract.with_records(policy, records)['dependencies']['lv2'])

    def test_release_asset_checked_against_github(self):
        published = {}

        def github(repository, tag, asset):
            published['asked'] = (repository, tag, asset)
            return digest(f'https://github.com/libsndfile/libsndfile/releases/download/{tag}/{asset}')

        records = self.resolve({'libsndfile'}, digest=github)
        self.assertEqual(published['asked'], ('libsndfile/libsndfile', '1.2.2', 'libsndfile-1.2.2.tar.xz'))
        self.assertEqual(records['libsndfile']['checked_against'], 'the digest GitHub publishes for the asset')
        self.assertIn('no published checksum', self.resolve({'lv2'})['lv2']['checked_against'])
        with self.assertRaisesRegex(resolve.ResolveError, 'but GitHub publishes ' + '0' * 64):
            self.resolve({'libsndfile'}, digest=lambda *_: '0' * 64)

    def test_a_submodule_follows_its_sdk(self):
        with self.assertRaisesRegex(resolve.ResolveError, "submodule follows its SDK; choose vst3sdk's version"):
            self.resolve({'vst3-base'}, {'vst3-base': '3.8.0'})

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
            self.assertEqual(resolve.load_records(path), resolve.without_notes(records))
            path.write_text(json.dumps({'schema': 1}))
            with self.assertRaisesRegex(ValueError, 'no dependency records'):
                resolve.load_records(path)
        table = resolve.markdown(records)
        self.assertIn(
            '| [lv2](https://codeload.github.com/lv2/lv2/tar.gz/' + '5' * 40 + ') | 1.18.10 | `v1.18.10` |', table
        )

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
            self.assertEqual((cache / 'x.tar.gz.sha256').read_text().strip(), digest('https://example.org/b'))

    def test_changed_cached_archive_is_refused(self):
        # The 0.5.2 review: changed cached bytes were recorded under a new checksum.
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            first = resolve.resolve(
                self.policy, cache, {'lv2'}, lister=lister, links=links, fetcher=fake_fetcher, digest=lambda *_: None
            )['lv2']
            archive = cache / first['archive']
            archive.write_bytes(b'altered')
            with self.assertRaises(resolve.ResolveError) as raised:
                resolve.resolve(
                    self.policy,
                    cache,
                    {'lv2'},
                    lister=lister,
                    links=links,
                    fetcher=fake_fetcher,
                    digest=lambda *_: None,
                )
            message = str(raised.exception)
            self.assertIn('has changed since it was downloaded', message)
            self.assertIn(first['sha256'], message)
            self.assertIn(hashlib.sha256(b'altered').hexdigest(), message)
            self.assertIn('Delete it', message)
            # Unchanged, it is reused without a download, with the same checksum.
            archive.write_bytes(first['url'].encode())

            def no_download(url, target):
                raise AssertionError(f'downloaded {url} again')

            again = resolve.resolve(
                self.policy, cache, {'lv2'}, lister=lister, links=links, fetcher=no_download, digest=lambda *_: None
            )['lv2']
            self.assertEqual(again['sha256'], first['sha256'])

    def test_failed_download_keeps_the_cached_archive(self):
        # The sidecars describe the archive beside them at every step: a
        # download that fails leaves the earlier archive reusable.
        calls = []

        def flaky(url, target):
            calls.append(url)
            if url.endswith('/b'):
                Path(target).write_bytes(b'partial')
                raise RuntimeError('connection reset')
            fake_fetcher(url, target)

        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            resolve.fetch_archive('https://example.org/a', cache, 'x.tar.gz', flaky)
            with self.assertRaisesRegex(RuntimeError, 'connection reset'):
                resolve.fetch_archive('https://example.org/b', cache, 'x.tar.gz', flaky)
            path = resolve.fetch_archive('https://example.org/a', cache, 'x.tar.gz', flaky)
            self.assertEqual(calls, ['https://example.org/a', 'https://example.org/b'])
            self.assertEqual(path.read_bytes(), b'https://example.org/a')
            self.assertEqual(sorted(p.name for p in cache.iterdir()), ['x.tar.gz', 'x.tar.gz.sha256', 'x.tar.gz.url'])

    def test_archive_cached_without_a_checksum_is_downloaded_again(self):
        calls = []

        def counting(url, target):
            calls.append(url)
            fake_fetcher(url, target)

        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            (cache / 'x.tar.gz').write_bytes(b'from an older cache')
            (cache / 'x.tar.gz.url').write_text('https://example.org/a\n')
            path = resolve.fetch_archive('https://example.org/a', cache, 'x.tar.gz', counting)
            self.assertEqual(calls, ['https://example.org/a'])
            self.assertEqual(path.read_bytes(), b'https://example.org/a')


class CheckoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = contract.load_policy(ROOT / 'packaging/release-policy.json')

    def test_newest_releases_in_build_order(self):
        cloned = []

        def cloner(remote, tag, directory):
            cloned.append((Path(directory).name, tag))
            return upstream.parse_ls_remote(listing(TAGS[remote]))[tag]

        records = resolve.checkout(self.policy, Path('sources'), lister=lister, cloner=cloner)
        # The advisory build tries lilv's release candidate, newer than 0.28.0.
        self.assertEqual(
            cloned,
            [
                ('zix', 'v0.8.2'),
                ('serd', 'v0.32.10'),
                ('sord', 'v0.16.22'),
                ('sratom', 'v0.6.22'),
                ('lilv', 'v0.30.0-rc1'),
            ],
        )
        self.assertEqual(records['lilv']['revision'], '9' * 40)
        self.assertIn('pre-release', records['lilv']['chosen'])
        self.assertEqual(records['zix']['chosen'], 'newest release')
        table = resolve.checkout_markdown(records)
        self.assertIn('| [lilv](https://gitlab.com/lv2/lilv.git) | 0.30.0-rc1 | `v0.30.0-rc1` |', table)

    def test_a_moved_tag_is_refused(self):
        with self.assertRaisesRegex(resolve.ResolveError, 'lilv: the clone of v0.30.0-rc1 is at ' + '0' * 40):
            resolve.checkout(self.policy, Path('sources'), ['lilv'], lister=lister, cloner=lambda *args: '0' * 40)
        with self.assertRaisesRegex(resolve.ResolveError, 'unknown test sources'):
            resolve.checkout(self.policy, Path('sources'), ['jack'], lister=lister)


class ToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = contract.load_policy(ROOT / 'packaging/release-policy.json')

    def tool(self, name, **kwargs):
        temporary = tempfile.TemporaryDirectory()  # pylint: disable=consider-using-with
        self.addCleanup(temporary.cleanup)
        return resolve.resolve_tool(
            self.policy,
            name,
            Path(temporary.name),
            kwargs.pop('os_name', None),
            kwargs.pop('wanted', None),
            lister=kwargs.pop('lister', lister),
            fetcher=kwargs.pop('fetcher', fake_fetcher),
            digest=kwargs.pop('digest', lambda *args: None),
            prerelease=kwargs.pop('prerelease', False),
        )

    def test_changed_cached_tool_is_not_replaced_from_a_mirror(self):
        # The independent review of the 0.5.2 fixes: the next mirror's URL
        # differs, so it would be downloaded over the changed archive.
        policy = {
            'release_tools': {
                'demo': {
                    'upstream': 'https://example.org/demo',
                    'tag_prefix': 'v',
                    'url': 'https://example.org/demo-{version}.tar.gz',
                    'mirrors': ['https://mirror.example.org/demo-{version}.tar.gz'],
                }
            }
        }

        def demo(remote):
            return listing({'v1.0': 'a' * 40})

        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            record, path = resolve.resolve_tool(
                policy, 'demo', cache, lister=demo, fetcher=fake_fetcher, digest=lambda *args: None
            )
            self.assertEqual(record['url'], 'https://example.org/demo-1.0.tar.gz')
            path.write_bytes(b'altered')
            with self.assertRaisesRegex(resolve.ResolveError, 'demo 1.0: the cached .* has changed'):
                resolve.resolve_tool(
                    policy, 'demo', cache, lister=demo, fetcher=fake_fetcher, digest=lambda *args: None
                )
            self.assertEqual(path.read_bytes(), b'altered')

    def test_nsis(self):
        record, path = self.tool('nsis')
        self.assertEqual((record['version'], record['tag']), ('3.13', 'v313'))
        self.assertEqual(record['url'], 'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.13/nsis-3.13.zip')
        self.assertEqual(path.name, 'nsis-3.13.zip')
        self.assertEqual(self.tool('nsis', wanted='3.11')[0]['version'], '3.11')

    def test_pluginval_per_os_with_published_digest(self):
        asset_url = 'https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip'
        record, _ = self.tool('pluginval', os_name='macOS', digest=lambda *args: digest(asset_url))
        self.assertEqual((record['version'], record['url']), ('1.0.4', asset_url))
        self.assertEqual(record['newer_prerelease'], 'v2.0.0-beta3')
        self.assertTrue(record['prerelease_tried'])  # The VST3 jobs run it; no extra warning.
        self.assertIn('newer pre-release exists upstream: `v2.0.0-beta3`', resolve.tool_markdown('pluginval', record))
        self.assertEqual(record['checked_against'], 'the digest GitHub publishes for the asset')
        with self.assertRaisesRegex(resolve.ResolveError, 'differs from the digest GitHub publishes'):
            self.tool('pluginval', os_name='Linux', digest=lambda *args: '0' * 64)
        with self.assertRaisesRegex(resolve.ResolveError, "no download for 'Plan9'"):
            self.tool('pluginval', os_name='Plan9')

    def test_pluginval_prerelease_is_advisory(self):
        record, path = self.tool('pluginval', os_name='Linux', prerelease=True)
        self.assertEqual(record['version'], '2.0.0-beta3')
        self.assertIn('advisory', record['chosen'])
        self.assertEqual(path.name, 'pluginval_Linux.zip')

        def released(remote):
            return listing({'v1.0.4': '1' * 40, 'v1.0.1-beta5': '2' * 40, 'v2.0.0': '3' * 40})

        self.assertEqual(self.tool('pluginval', os_name='Linux', prerelease=True, lister=released), (None, None))
        self.assertEqual(self.tool('pluginval', os_name='Linux', lister=released)[0]['version'], '2.0.0')

    def test_prerelease_on_the_command_line(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'out'
            with mock.patch.object(resolve, 'resolve_tool', return_value=(None, None)), contextlib.redirect_stdout(
                io.StringIO()
            ) as printed:
                self.assertEqual(
                    resolve.main(
                        ['tool', 'pluginval', '--prerelease', '--cache', temporary, '--github-output', str(output)]
                    ),
                    0,
                )
            self.assertEqual(output.read_text(), 'available=false\n')
            self.assertIn('No pre-release of pluginval is newer', printed.getvalue())

    def test_lv2lint_falls_back_to_freebsd_ports(self):
        distinfo = (
            'TIMESTAMP = 1\nSHA256 (lv2lint-0.16.2.tar.xz) = '
            + digest('https://git.open-music-kontrollers.ch/~hp/lv2lint/snapshot/lv2lint-0.16.2.tar.xz')
            + '\nSIZE (lv2lint-0.16.2.tar.xz) = 51984\n'
        )

        def fetcher(url, target):
            if url.endswith('/distinfo'):
                Path(target).write_text(distinfo)
            else:
                fake_fetcher(url, target)

        def unreachable(remote):
            raise RuntimeError('CONNECT tunnel failed')

        record, _ = self.tool('lv2lint', lister=unreachable, fetcher=fetcher)
        self.assertEqual(record['version'], '0.16.2')
        self.assertTrue(record['url'].startswith('https://git.open-music-kontrollers.ch/'))
        self.assertEqual(record['checked_against'], 'FreeBSD ports distinfo')
        self.assertIn('did not answer (CONNECT tunnel failed)', record['chosen'])

    def test_plain_http_mirror_only_with_a_published_checksum(self):
        good = 'http://distcache.FreeBSD.org/ports-distfiles/lv2lint-0.16.2.tar.xz'

        def fetcher(url, target):
            if url.endswith('/distinfo'):
                Path(target).write_text(f'SHA256 (lv2lint-0.16.2.tar.xz) = {digest(good)}\n')
            elif url.startswith('https://'):
                raise RuntimeError(f'cannot download {url}: Not Found')
            else:
                fake_fetcher(url, target)

        def listed(remote):
            return listing({'0.16.2': 'a' * 40})

        record, _ = self.tool('lv2lint', lister=listed, fetcher=fetcher)
        self.assertEqual((record['url'], record['checked_against']), (good, 'FreeBSD ports distinfo'))

        def no_distinfo(url, target):
            if url.endswith('/distinfo'):
                raise RuntimeError('unreachable')
            fetcher(url, target)

        with self.assertRaisesRegex(resolve.ResolveError, 'not HTTPS, and there is no published checksum'):
            self.tool('lv2lint', lister=listed, fetcher=no_distinfo)

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
