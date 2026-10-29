# SPDX-License-Identifier: GPL-2.0-or-later
"""Resolve each supporting library and tool to its newest release, and record it.

Builds use the newest upstream release of every SDK and library, not a pinned
one (CI-6, docs/develop/build.md#supporting-libraries). The release policy
says where each one comes from (`upstream`, how its tags name releases, and
the archive `url`); this script lists the upstream tags, picks the newest
release (or one asked for with --version NAME=VERSION), downloads its archive
and records exactly what was used:

  version   the release, as its tag names it
  tag       the upstream tag
  revision  the commit that tag points at (for VST3 SDK submodules, the
            commit the SDK's tag records for them)
  url       where the archive came from
  sha256    the archive's checksum

The record (a lock file, or the `dependencies` of a source manifest) rebuilds
the same sources later: sources.py --lock FILE fetches exactly those archives
and refuses any whose checksum differs.

python3 scripts/release/resolve.py dependencies --cache DIR --lock FILE [NAME ...]
Pre-releases are never used for builds or formal tests. A newer one is named
in the record (`newer_prerelease`), and `tool --prerelease` fetches it for an
advisory run.

python3 scripts/release/resolve.py tool pluginval --cache DIR [--os Linux] [--prerelease]
python3 scripts/release/resolve.py checkout --destination DIR [NAME ...]   (test sources, advisory builds)
python3 scripts/release/resolve.py packages PACKAGE ...   (installed Debian packages, for the summary)
python3 scripts/release/resolve.py explain-failure [--suspect NAME]   (after a failed CI step)
"""

import argparse
import datetime
import json
import os
from pathlib import Path
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

from contract import load_policy, sha256, validate_records
import upstream

ROOT = Path(__file__).resolve().parents[2]
POLICY = ROOT / 'packaging/release-policy.json'
# Copied from the policy rule into each record, so a record alone can unpack,
# build and credit its dependency.
NOTE_FIELDS = ('newer_prerelease', 'prerelease_tried')
RULE_FIELDS = ('upstream', 'archive', 'directory', 'license', 'notice', 'formats', 'submodule_of', 'path')
HELP = 'Builds use the newest upstream release of each dependency (docs/develop/build.md#supporting-libraries).'


class ResolveError(ValueError):
    """One or more dependencies could not be resolved; the message lists each and why."""


class CachedArchiveChanged(RuntimeError):
    """A cached archive no longer has the checksum of its first download. Never
    worked around by another URL: it is reported, so the cache can be checked."""


def download(url, target, attempts=3):
    """Downloads url to target, retrying a flaky host twice (after 5 s, then 10 s)."""
    request = urllib.request.Request(url, headers={'User-Agent': 'convolver-release-scripts'})
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(request, timeout=300) as response, target.open('wb') as output:
                shutil.copyfileobj(response, output)
            return
        except (OSError, urllib.error.URLError) as failure:
            error = failure
        if attempt + 1 < attempts:
            time.sleep(5 * 2**attempt)
    raise RuntimeError(f'cannot download {url}: {getattr(error, "reason", error)}')


def _sidecar(path):
    """The text of a cache sidecar file, or None if there is none."""
    return path.read_text(encoding='utf-8').strip() if path.is_file() else None


def fetch_archive(url, cache, name, fetcher=download):
    """The archive at url, in cache under name. An archive already there is
    reused only if it came from the same URL (the URLs name an exact commit or
    release, so their contents do not change) and still has the checksum it
    had when it was downloaded. Most dependencies have no published checksum,
    so that first download's checksum is the one trusted: a cached archive
    whose bytes have changed since is refused rather than recorded under a new
    checksum. One cached without a checksum (by an older version of this
    script) is downloaded again."""
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / name
    origin = cache / (name + '.url')
    checksum = cache / (name + '.sha256')
    if path.is_file() and _sidecar(origin) == url:
        first = _sidecar(checksum)
        if first:
            actual = sha256(path)
            if actual != first:
                raise CachedArchiveChanged(
                    f'the cached {path} has changed since it was downloaded from {url}: its '
                    f'SHA-256 is now {actual}, not {first}. Delete it, with {origin.name} and '
                    f'{checksum.name}, to download it again'
                )
            return path
    with tempfile.NamedTemporaryFile(dir=cache, delete=False) as temporary:
        partial = Path(temporary.name)
    try:
        fetcher(url, partial)
        downloaded = sha256(partial)
        # The sidecars go first, so an interrupted download never leaves an
        # archive described by another one's URL and checksum.
        origin.unlink(missing_ok=True)
        checksum.unlink(missing_ok=True)
        partial.replace(path)
    finally:
        partial.unlink(missing_ok=True)
    checksum.write_text(downloaded + '\n', encoding='utf-8')
    origin.write_text(url + '\n', encoding='utf-8')
    return path


def submodule_url(parent, url):
    """A .gitmodules URL, resolved against the parent repository's URL if relative."""
    if not url.startswith(('./', '../')):
        return url
    parsed = urllib.parse.urlsplit(parent.rstrip('/'))
    path = posixpath.normpath(posixpath.join(parsed.path, url))
    return urllib.parse.urlunsplit(parsed._replace(path=path))


def expand(rules, names):
    unknown = set(names) - set(rules)
    if unknown:
        raise ResolveError(f'unknown dependencies: {sorted(unknown)} (the release policy has {sorted(rules)})')
    expanded = set(names)
    for name in names:
        if 'submodule_of' in rules[name]:
            expanded.add(rules[name]['submodule_of'])
    return expanded


def _record(rule, version, tag, revision, chosen, cache, fetcher, digest):
    url = rule['url'].format(version=version, tag=tag, revision=revision)
    archive = fetch_archive(url, cache, rule['archive'], fetcher)
    actual = sha256(archive)
    against = 'nothing (no published checksum)'
    if rule.get('github_release'):
        published = digest(rule['github_release'], tag, posixpath.basename(urllib.parse.urlsplit(url).path))
        if published and published != actual:
            archive.unlink()
            raise ValueError(f'{url} has checksum {actual}, but GitHub publishes {published} for it')
        if published:
            against = 'the digest GitHub publishes for the asset'
    record = {key: rule[key] for key in RULE_FIELDS if key in rule}
    record.update(
        version=version, tag=tag, revision=revision, url=url, sha256=actual, chosen=chosen, checked_against=against
    )
    return record


def resolve(
    policy,
    cache,
    names=None,
    wanted=None,
    lister=upstream.ls_remote,
    links=upstream.gitlinks,
    fetcher=download,
    digest=None,
):
    """Records for the named dependencies (all by default), each the newest
    release, or the version `wanted` names for it; every archive is fetched
    into cache. Raises ResolveError naming every dependency that failed."""
    rules = policy['dependencies']
    wanted = dict(wanted or {})
    names = expand(rules, names if names is not None else rules)
    digest = digest or _published_digest
    unknown = set(wanted) - names
    if unknown:
        raise ResolveError(f'--version names dependencies that are not being resolved: {sorted(unknown)}')
    submodules = sorted(name for name in wanted if 'submodule_of' in rules[name])
    if submodules:
        raise ResolveError(
            f'--version cannot choose {", ".join(submodules)}: a submodule follows its SDK; '
            f"choose {rules[submodules[0]]['submodule_of']}'s version instead"
        )
    records, errors = {}, []
    parents = {}
    for name in sorted(n for n in names if 'submodule_of' not in rules[n]):
        rule = rules[name]
        try:
            tags = upstream.parse_ls_remote(lister(rule['upstream']))
            version, tag, revision = upstream.newest(rule, tags, wanted.get(name))
            chosen = f'requested ({version})' if name in wanted else 'newest release'
            parents[name] = (version, tag, chosen)
            records[name] = _record(rule, version, tag, revision, chosen, cache, fetcher, digest)
            _note_prerelease(records[name], rule, tags)
        except (OSError, RuntimeError, ValueError) as error:
            errors.append(f'{name}: {error}')
    for parent in sorted({rules[n]['submodule_of'] for n in names if 'submodule_of' in rules[n]}):
        if parent not in parents:
            continue  # Its own failure is already reported.
        version, tag, chosen = parents[parent]
        children = sorted(n for n in names if rules[n].get('submodule_of') == parent)
        try:
            commits, urls = links(rules[parent]['upstream'], tag, [rules[n]['path'] for n in children])
        except (OSError, RuntimeError, ValueError) as error:
            errors.append(f'{parent} submodules {", ".join(children)}: cannot read them at {tag}: {error}')
            continue
        for name in children:
            rule = rules[name]
            path = rule['path']
            try:
                if path not in commits:
                    raise ValueError(
                        f"{parent} {tag} has no submodule at {path}; the SDK's layout changed, "
                        'so the release policy and the build need updating'
                    )
                found = submodule_url(rules[parent]['upstream'], urls.get(path, ''))
                if not upstream.same_repository(found, rule['upstream']):
                    raise ValueError(
                        f'{parent} {tag} takes {path} from {found or "nowhere"}, '
                        f'not {rule["upstream"]}; update the release policy'
                    )
                records[name] = _record(
                    rule, version, tag, commits[path], f'{chosen}, from {parent} {tag}', cache, fetcher, digest
                )
            except (OSError, RuntimeError, ValueError) as error:
                errors.append(f'{name}: {error}')
    if errors:
        raise ResolveError(
            'Cannot resolve every dependency to an upstream release:\n  ' + '\n  '.join(errors) + '\n' + HELP
        )
    validate_records(records)
    return records


def _note_prerelease(record, rule, tags):
    """Names a newer beta or release candidate, which builds do not use but
    which may signpost changes to come."""
    if rule.get('prereleases'):
        return
    found = upstream.newest_prerelease(rule, tags)
    if found:
        record['newer_prerelease'] = found[1]
        if rule.get('advisory_prerelease_run'):
            record['prerelease_tried'] = True


def prerelease_notes(records):
    """Markdown lines naming the newer pre-releases that were not used."""
    notes = [
        f'- {name}: `{record["newer_prerelease"]}` (not used; builds and formal tests use {record["version"]})'
        for name, record in sorted(records.items())
        if record.get('newer_prerelease')
    ]
    return ('\nNewer pre-releases upstream:\n\n' + '\n'.join(notes) + '\n') if notes else ''


def warn_prereleases(records):
    """A GitHub Actions warning for each newer pre-release that no advisory run
    tries, so that what it signposts is looked at (its release notes)."""
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        return
    for name, record in sorted(records.items()):
        if record.get('newer_prerelease') and not record.get('prerelease_tried'):
            print(
                f'::warning title=Newer {name} pre-release::{name} {record["newer_prerelease"]} is out upstream; '
                f'builds and formal tests use {record["version"]}. Check its release notes for changes to come.'
            )
    sys.stdout.flush()


def without_notes(records):
    """The records without what only describes upstream at resolution time
    (a newer pre-release), which would be stale in a lock file or manifest."""
    return {
        name: {key: value for key, value in record.items() if key not in NOTE_FIELDS}
        for name, record in records.items()
    }


def lock(records, resolved_at=None):
    """A lock file's contents: what was resolved and when."""
    resolved_at = resolved_at or datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    return {'schema': 1, 'resolved_at': resolved_at, 'dependencies': without_notes(records)}


def load_records(path):
    """The dependency records of a lock file or a source manifest."""
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    records = data.get('dependencies')
    if not isinstance(records, dict) or not records:
        raise ValueError(f'{path}: no dependency records (expected a lock file or a source manifest)')
    validate_records(records)
    return records


def markdown(records, heading='Dependencies used'):
    """The records as a Markdown table, for a job summary or a terminal."""
    lines = [
        f'### {heading}',
        '',
        '| Dependency | Version | Tag | Commit | Archive SHA-256 | Chosen |',
        '| --- | --- | --- | --- | --- | --- |',
    ]
    for name in sorted(records):
        record = records[name]
        lines.append(
            f'| [{name}]({record["url"]}) | {record["version"]} | `{record.get("tag", "-")}` | '
            f'`{record.get("revision", "-")}` | `{record["sha256"]}` | {record.get("chosen", "recorded")}'
            f'{"; matches GitHub digest" if record.get("checked_against", "").startswith("the digest") else ""} |'
        )
    return '\n'.join(lines) + '\n' + prerelease_notes(records)


def report(text):
    """Prints text, and adds it to the GitHub Actions job summary when there is one."""
    sys.stdout.write(text)
    sys.stdout.flush()
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as stream:
            stream.write(text + '\n')


def parse_wanted(values):
    wanted = {}
    for value in values or []:
        name, _, version = value.partition('=')
        if not name or not version:
            raise ValueError(f'--version takes NAME=VERSION, not {value!r}')
        wanted[name] = version
    return wanted


# Tools (NSIS for the installer, pluginval and lv2lint for the tests) are
# resolved the same way, one at a time.


def _published_digest(repository, tag, asset):
    """The sha256 GitHub publishes for a release asset, or None if it cannot be read."""
    request = urllib.request.Request(
        f'https://api.github.com/repos/{repository}/releases/tags/{tag}',
        headers={'Accept': 'application/vnd.github+json', 'User-Agent': 'convolver-release-scripts'},
    )
    token = os.environ.get('GITHUB_TOKEN')
    if token:
        request.add_header('Authorization', f'Bearer {token}')
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            release = json.load(response)
    except (OSError, urllib.error.URLError, ValueError):
        return None
    for item in release.get('assets', []):
        if item.get('name') == asset and str(item.get('digest', '')).startswith('sha256:'):
            return item['digest'][len('sha256:') :]
    return None


def _distinfo(url, fetcher):
    """{version: sha256} from a FreeBSD ports distinfo file."""
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / 'distinfo'
        fetcher(url, path)
        text = path.read_text(encoding='utf-8', errors='replace')
    return {
        match[1]: match[2]
        for match in re.finditer(r'^SHA256 \([^)]*?-(\d[\w.]*)\.tar\.\w+\) = ([0-9a-f]{64})$', text, re.MULTILINE)
    }


def resolve_tool(
    policy,
    name,
    cache,
    os_name=None,
    wanted=None,
    lister=upstream.ls_remote,
    fetcher=download,
    digest=_published_digest,
    prerelease=False,
):
    """(record, archive path) for a release or test tool from the policy.

    With prerelease, the newest beta or release candidate that is newer than
    the newest release instead, for an advisory run; (None, None) if there is
    none."""
    rules = dict(policy.get('release_tools', {}), **policy.get('test_tools', {}))
    if name not in rules:
        raise ResolveError(f'unknown tool {name} (the release policy has {sorted(rules)})')
    rule = rules[name]
    expected, against = None, 'nothing (no published checksum)'
    newer = None
    try:
        tags = upstream.parse_ls_remote(lister(rule['upstream']))
        if prerelease:
            found = upstream.newest_prerelease(rule, tags)
            if found is None:
                return None, None
            version, tag, revision = found
            chosen = 'newest pre-release, newer than the newest release (advisory)'
        else:
            version, tag, revision = upstream.newest(rule, tags, wanted)
            chosen = f'requested ({version})' if wanted else 'newest release'
            found = None if rule.get('prereleases') else upstream.newest_prerelease(rule, tags)
            newer = found[1] if found else None
    except (OSError, RuntimeError) as error:
        # A small host with bad days: a packager's record of the newest release
        # and its checksum (FreeBSD's ports) stands in, and says so.
        if 'distinfo' not in rule or wanted or prerelease:
            raise ResolveError(f'{name}: cannot list the releases at {rule["upstream"]}: {error}\n{HELP}') from error
        known = _distinfo_or_empty(rule['distinfo'], fetcher)
        if not known:
            raise ResolveError(
                f'{name}: {rule["upstream"]} did not answer ({error}), and {rule["distinfo"]} lists no release either'
            ) from error
        version = max(known, key=upstream.version_key)
        tag, revision = rule.get('tag_prefix', '') + version, ''
        expected, against = known[version], 'FreeBSD ports distinfo'
        chosen = f'newest in FreeBSD ports, because {rule["upstream"]} did not answer ({error})'
    except ValueError as error:
        raise ResolveError(f'{name}: {error}\n{HELP}') from error
    asset = ''
    if 'assets' in rule:
        if os_name not in rule['assets']:
            raise ResolveError(f'{name}: no download for {os_name!r}; the policy lists {sorted(rule["assets"])}')
        asset = rule['assets'][os_name]
    urls = [
        template.format(version=version, tag=tag, asset=asset) for template in [rule['url'], *rule.get('mirrors', [])]
    ]
    archive_name = asset or posixpath.basename(urllib.parse.urlsplit(urls[0]).path)
    if rule.get('github_release'):
        expected = digest(rule['github_release'], tag, asset)
        if expected:
            against = 'the digest GitHub publishes for the asset'
    elif rule.get('distinfo') and not expected:
        published = _distinfo_or_empty(rule['distinfo'], fetcher)
        if version in published:
            expected, against = published[version], 'FreeBSD ports distinfo'
    failures = []
    for url in urls:
        if not url.startswith('https://') and not expected:
            # A plain-HTTP mirror is trusted only through a checksum fetched over HTTPS.
            failures.append(f'{url}: not HTTPS, and there is no published checksum to check it against')
            continue
        try:
            path = fetch_archive(url, cache, archive_name, fetcher)
        except CachedArchiveChanged as error:
            raise ResolveError(f'{name} {version}: {error}') from error
        except (OSError, RuntimeError) as error:
            failures.append(str(error))
            continue
        actual = sha256(path)
        if expected and actual != expected:
            failures.append(f'{url}: checksum {actual} differs from {against} ({expected})')
            path.unlink()
            continue
        record = {
            'version': version,
            'tag': tag,
            'url': url,
            'sha256': actual,
            'chosen': chosen,
            'checked_against': against,
            'upstream': rule['upstream'],
        }
        if revision:
            record['revision'] = revision
        if newer:
            record['newer_prerelease'] = newer
            if rule.get('advisory_prerelease_run'):
                record['prerelease_tried'] = True
        return record, path
    raise ResolveError(f'{name} {version}: no download succeeded:\n  ' + '\n  '.join(failures) + f'\n{HELP}')


def _clone(remote, tag, directory):
    upstream._git(
        [
            '-c',
            'advice.detachedHead=false',
            'clone',
            '--quiet',
            '--depth',
            '1',
            '--branch',
            tag,
            remote,
            str(directory),
        ],
        timeout=600,
    )
    return upstream._git(['rev-parse', 'HEAD'], cwd=directory).strip()


def checkout(policy, destination, names=None, lister=upstream.ls_remote, cloner=_clone):
    """Clones the newest release of each test source (all, in the policy's
    order, by default) into destination/NAME; returns their records. Only
    advisory jobs build these, so the formal tests never depend on them, and a
    pre-release newer than the newest release is taken: it is what such a job
    is for."""
    rules = policy.get('test_sources', {})
    names = list(rules) if not names else names
    unknown = sorted(set(names) - set(rules))
    if unknown:
        raise ResolveError(f'unknown test sources: {unknown} (the release policy has {sorted(rules)})')
    records, errors = {}, []
    for name in names:
        rule = rules[name]
        try:
            tags = upstream.parse_ls_remote(lister(rule['upstream']))
            found = upstream.newest_prerelease(rule, tags)
            version, tag, revision = found or upstream.newest(rule, tags)
            commit = cloner(rule['upstream'], tag, Path(destination) / name)
            if commit != revision:
                raise ValueError(f'the clone of {tag} is at {commit}, but the tag points at {revision}')
            records[name] = {
                'version': version,
                'tag': tag,
                'revision': revision,
                'url': rule['upstream'],
                'chosen': 'newest pre-release, newer than the newest release' if found else 'newest release',
            }
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
            errors.append(f'{name}: {error}')
    if errors:
        raise ResolveError('Cannot check out every test source:\n  ' + '\n  '.join(errors) + '\n' + HELP)
    return records


def checkout_markdown(records):
    lines = [
        '### Newest upstream sources (advisory build)',
        '',
        '| Source | Version | Tag | Commit |',
        '| --- | --- | --- | --- |',
    ]
    for name, record in records.items():
        lines.append(
            f'| [{name}]({record["url"]}) | {record["version"]} | `{record["tag"]}` | `{record["revision"]}` |'
        )
    return '\n'.join(lines) + '\n' + prerelease_notes(records)


def _distinfo_or_empty(url, fetcher):
    try:
        return _distinfo(url, fetcher)
    except (OSError, RuntimeError, ValueError):
        return {}


def tool_markdown(name, record):
    return (
        f'### {name} used\n\n{name} {record["version"]} (tag `{record["tag"]}`'
        + (f', commit `{record["revision"]}`' if record.get('revision') else '')
        + f'), from {record["url"]}, SHA-256 `{record["sha256"]}`, checked against '
        f'{record["checked_against"]}; chosen: {record["chosen"]}.\n'
        + (
            f'\nA newer pre-release exists upstream: `{record["newer_prerelease"]}` (not used here).\n'
            if record.get('newer_prerelease')
            else ''
        )
    )


def packages_markdown(names, query=None):
    """The installed versions of Debian packages, as a Markdown table."""
    query = query or (
        lambda packages: (
            subprocess.run(
                ['dpkg-query', '-W', '-f', '${Package}\t${Version}\n', *packages],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
        )
    )
    lines = ['### Ubuntu packages used', '', '| Package | Version |', '| --- | --- |']
    for line in sorted(query(names).splitlines()):
        package, _, version = line.partition('\t')
        lines.append(f'| {package} | {version} |')
    return '\n'.join(lines) + '\n'


def failure_explanation(suspect=None):
    """What to check when a CI step fails, given that builds follow upstream releases."""
    which = f'a new {suspect} release' if suspect else 'a new upstream release'
    return (
        '### If nothing in Convolver changed\n\n'
        'This run used the newest release of each SDK, library and tool (CI-6); the summary above lists '
        'each version, commit and checksum. If the same Convolver commit passed before, compare them with '
        f'the last green run: {which} is then the likely cause. To confirm, rebuild with the previous '
        'release, for example `python3 scripts/release/sources.py --latest --fetch --version '
        f'{suspect or "NAME"}=OLD_VERSION --cache build/downloads --destination build/sources '
        f'{suspect or "NAME"}`, and then adapt Convolver to the new release in a pull request '
        '(docs/develop/build.md#when-a-new-release-breaks-the-build).\n'
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0], epilog='See docs/develop/build.md#supporting-libraries.'
    )
    parser.add_argument('--policy', type=Path, default=POLICY)
    commands = parser.add_subparsers(dest='command', required=True)
    deps = commands.add_parser('dependencies', help='resolve SDKs and libraries and write a lock file')
    deps.add_argument('--cache', type=Path, required=True, help='directory for the downloaded archives')
    deps.add_argument('--lock', type=Path, required=True, help='lock file to write')
    deps.add_argument(
        '--version', action='append', metavar='NAME=VERSION', help='use this release instead of the newest (repeatable)'
    )
    deps.add_argument('names', nargs='*', help='dependency names from the policy (default: all)')
    tool = commands.add_parser('tool', help='download the newest release of a tool')
    tool.add_argument('name')
    tool.add_argument('--cache', type=Path, required=True)
    tool.add_argument('--os', dest='os_name', help='Linux, macOS or Windows, for tools with one download per OS')
    tool.add_argument('--version', help='use this release instead of the newest')
    tool.add_argument('--github-output', type=Path, help='append available=, archive=, version= and sha256= here')
    tool.add_argument(
        '--prerelease',
        action='store_true',
        help='the newest beta or release candidate newer than the newest release, for an advisory '
        'run; reports available=false when there is none',
    )
    sources = commands.add_parser('checkout', help='clone the newest release of test sources (advisory builds)')
    sources.add_argument('--destination', type=Path, required=True)
    sources.add_argument('--github-output', type=Path, help='append NAME=DIRECTORY here')
    sources.add_argument('names', nargs='*', help='test sources from the policy (default: all, in build order)')
    packages = commands.add_parser('packages', help='report the installed versions of Debian packages')
    packages.add_argument('packages', nargs='+')
    explain = commands.add_parser('explain-failure', help='say how to tell whether a new release broke CI')
    explain.add_argument('--suspect', help='the dependency most likely to need code changes')
    args = parser.parse_args(argv)
    if args.command == 'packages':
        report(packages_markdown(args.packages))
        return 0
    if args.command == 'explain-failure':
        report(failure_explanation(args.suspect))
        if os.environ.get('GITHUB_ACTIONS') == 'true':
            print(
                '::notice title=Newest releases::This run built with the newest upstream releases; if '
                'Convolver did not change, see the job summary for what to check.'
            )
        return 0
    policy = load_policy(args.policy)
    try:
        if args.command == 'dependencies':
            records = resolve(policy, args.cache.resolve(), args.names or None, parse_wanted(args.version))
            args.lock.write_text(json.dumps(lock(records), indent=2) + '\n', encoding='utf-8')
            report(markdown(records))
            warn_prereleases(records)
        elif args.command == 'checkout':
            destination = args.destination.resolve()
            records = checkout(policy, destination, args.names or None)
            report(checkout_markdown(records))
            if args.github_output:
                with args.github_output.open('a', encoding='utf-8') as output:
                    output.writelines(f'{name}={(destination / name).as_posix()}\n' for name in records)
        else:
            if args.prerelease and args.version:
                raise ResolveError('--prerelease takes the newest pre-release; it cannot be combined with --version')
            record, path = resolve_tool(
                policy, args.name, args.cache.resolve(), args.os_name, args.version, prerelease=args.prerelease
            )
            if record is None:
                report(
                    f'### {args.name} pre-release\n\nNo pre-release of {args.name} is newer than its newest '
                    'release, so there is nothing more to try.\n'
                )
            else:
                report(tool_markdown(args.name, record))
                warn_prereleases({args.name: record})
            if args.github_output:
                with args.github_output.open('a', encoding='utf-8') as output:
                    if record is None:
                        output.write('available=false\n')
                    else:
                        output.write(
                            f'available=true\narchive={path.as_posix()}\nversion={record["version"]}\n'
                            f'sha256={record["sha256"]}\n'
                        )
    except ResolveError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
