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
python3 scripts/release/resolve.py tool pluginval --cache DIR [--os Linux]
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
RULE_FIELDS = ('upstream', 'archive', 'directory', 'license', 'notice', 'formats', 'submodule_of', 'path')
HELP = ('Builds use the newest upstream release of each dependency '
        '(docs/develop/build.md#supporting-libraries).')


class ResolveError(ValueError):
    """One or more dependencies could not be resolved; the message lists each and why."""


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


def fetch_archive(url, cache, name, fetcher=download):
    """The archive at url, in cache under name. An archive already there is
    reused only if it came from the same URL (the URLs name an exact commit or
    release, so their contents do not change)."""
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / name
    origin = cache / (name + '.url')
    if path.is_file() and origin.is_file() and origin.read_text(encoding='utf-8').strip() == url:
        return path
    with tempfile.NamedTemporaryFile(dir=cache, delete=False) as temporary:
        partial = Path(temporary.name)
    try:
        fetcher(url, partial)
        partial.replace(path)
    finally:
        partial.unlink(missing_ok=True)
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


def _record(rule, version, tag, revision, chosen, cache, fetcher):
    url = rule['url'].format(version=version, tag=tag, revision=revision)
    archive = fetch_archive(url, cache, rule['archive'], fetcher)
    record = {key: rule[key] for key in RULE_FIELDS if key in rule}
    record.update(version=version, tag=tag, revision=revision, url=url, sha256=sha256(archive), chosen=chosen)
    return record


def resolve(policy, cache, names=None, wanted=None, lister=upstream.ls_remote, links=upstream.gitlinks,
            fetcher=download):
    """Records for the named dependencies (all by default), each the newest
    release, or the version `wanted` names for it; every archive is fetched
    into cache. Raises ResolveError naming every dependency that failed."""
    rules = policy['dependencies']
    wanted = dict(wanted or {})
    names = expand(rules, names if names is not None else rules)
    unknown = set(wanted) - names
    if unknown:
        raise ResolveError(f'--version names dependencies that are not being resolved: {sorted(unknown)}')
    records, errors = {}, []
    parents = {}
    for name in sorted(n for n in names if 'submodule_of' not in rules[n]):
        rule = rules[name]
        try:
            version, tag, revision = upstream.newest(rule, upstream.parse_ls_remote(lister(rule['upstream'])),
                                                     wanted.get(name))
            chosen = f'requested ({version})' if name in wanted else 'newest release'
            parents[name] = (version, tag, chosen)
            records[name] = _record(rule, version, tag, revision, chosen, cache, fetcher)
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
                    raise ValueError(f'{parent} {tag} has no submodule at {path}; the SDK\'s layout changed, '
                                     'so the release policy and the build need updating')
                found = submodule_url(rules[parent]['upstream'], urls.get(path, ''))
                if not upstream.same_repository(found, rule['upstream']):
                    raise ValueError(f'{parent} {tag} takes {path} from {found or "nowhere"}, '
                                     f'not {rule["upstream"]}; update the release policy')
                records[name] = _record(rule, version, tag, commits[path], f'{chosen}, from {parent} {tag}',
                                        cache, fetcher)
            except (OSError, RuntimeError, ValueError) as error:
                errors.append(f'{name}: {error}')
    if errors:
        raise ResolveError('Cannot resolve every dependency to an upstream release:\n  ' + '\n  '.join(errors) +
                           '\n' + HELP)
    validate_records(records)
    return records


def lock(records, resolved_at=None):
    """A lock file's contents: what was resolved and when."""
    resolved_at = resolved_at or datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    return {'schema': 1, 'resolved_at': resolved_at, 'dependencies': records}


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
    lines = [f'### {heading}', '',
             '| Dependency | Version | Tag | Commit | Archive SHA-256 | Chosen |',
             '| --- | --- | --- | --- | --- | --- |']
    for name in sorted(records):
        record = records[name]
        lines.append(f'| [{name}]({record["url"]}) | {record["version"]} | `{record.get("tag", "-")}` | '
                     f'`{record.get("revision", "-")}` | `{record["sha256"]}` | {record.get("chosen", "recorded")} |')
    return '\n'.join(lines) + '\n'


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
    request = urllib.request.Request(f'https://api.github.com/repos/{repository}/releases/tags/{tag}',
                                     headers={'Accept': 'application/vnd.github+json',
                                              'User-Agent': 'convolver-release-scripts'})
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
            return item['digest'][len('sha256:'):]
    return None


def _distinfo(url, fetcher):
    """{version: sha256} from a FreeBSD ports distinfo file."""
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / 'distinfo'
        fetcher(url, path)
        text = path.read_text(encoding='utf-8', errors='replace')
    return {match[1]: match[2] for match in
            re.finditer(r'^SHA256 \([^)]*?-(\d[\w.]*)\.tar\.\w+\) = ([0-9a-f]{64})$', text, re.MULTILINE)}


def resolve_tool(policy, name, cache, os_name=None, wanted=None, lister=upstream.ls_remote, fetcher=download,
                 digest=_published_digest):
    """(record, archive path) for a release or test tool from the policy."""
    rules = dict(policy.get('release_tools', {}), **policy.get('test_tools', {}))
    if name not in rules:
        raise ResolveError(f'unknown tool {name} (the release policy has {sorted(rules)})')
    rule = rules[name]
    expected, against = None, 'nothing (no published checksum)'
    try:
        version, tag, revision = upstream.newest(rule, upstream.parse_ls_remote(lister(rule['upstream'])), wanted)
        chosen = f'requested ({version})' if wanted else 'newest release'
    except (OSError, RuntimeError) as error:
        # A small host with bad days: a packager's record of the newest release
        # and its checksum (FreeBSD's ports) stands in, and says so.
        if 'distinfo' not in rule or wanted:
            raise ResolveError(f'{name}: cannot list the releases at {rule["upstream"]}: {error}\n{HELP}') from error
        known = _distinfo_or_empty(rule['distinfo'], fetcher)
        if not known:
            raise ResolveError(f'{name}: {rule["upstream"]} did not answer ({error}), and {rule["distinfo"]} '
                               'lists no release either') from error
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
    urls = [template.format(version=version, tag=tag, asset=asset)
            for template in [rule['url'], *rule.get('mirrors', [])]]
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
        except (OSError, RuntimeError) as error:
            failures.append(str(error))
            continue
        actual = sha256(path)
        if expected and actual != expected:
            failures.append(f'{url}: checksum {actual} differs from {against} ({expected})')
            path.unlink()
            continue
        record = {'version': version, 'tag': tag, 'url': url, 'sha256': actual, 'chosen': chosen,
                  'checked_against': against, 'upstream': rule['upstream']}
        if revision:
            record['revision'] = revision
        return record, path
    raise ResolveError(f'{name} {version}: no download succeeded:\n  ' + '\n  '.join(failures) + f'\n{HELP}')


def _distinfo_or_empty(url, fetcher):
    try:
        return _distinfo(url, fetcher)
    except (OSError, RuntimeError, ValueError):
        return {}


def tool_markdown(name, record):
    return (f'### {name} used\n\n{name} {record["version"]} (tag `{record["tag"]}`'
            + (f', commit `{record["revision"]}`' if record.get('revision') else '') +
            f'), from {record["url"]}, SHA-256 `{record["sha256"]}`, checked against '
            f'{record["checked_against"]}; chosen: {record["chosen"]}.\n')


def packages_markdown(names, query=None):
    """The installed versions of Debian packages, as a Markdown table."""
    query = query or (lambda packages: subprocess.run(
        ['dpkg-query', '-W', '-f', '${Package}\t${Version}\n', *packages], capture_output=True, text=True,
        check=True).stdout)
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
        '(docs/develop/build.md#when-a-new-release-breaks-the-build).\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     epilog='See docs/develop/build.md#supporting-libraries.')
    parser.add_argument('--policy', type=Path, default=POLICY)
    commands = parser.add_subparsers(dest='command', required=True)
    deps = commands.add_parser('dependencies', help='resolve SDKs and libraries and write a lock file')
    deps.add_argument('--cache', type=Path, required=True, help='directory for the downloaded archives')
    deps.add_argument('--lock', type=Path, required=True, help='lock file to write')
    deps.add_argument('--version', action='append', metavar='NAME=VERSION',
                      help='use this release instead of the newest (repeatable)')
    deps.add_argument('names', nargs='*', help='dependency names from the policy (default: all)')
    tool = commands.add_parser('tool', help='download the newest release of a tool')
    tool.add_argument('name')
    tool.add_argument('--cache', type=Path, required=True)
    tool.add_argument('--os', dest='os_name', help='Linux, macOS or Windows, for tools with one download per OS')
    tool.add_argument('--version', help='use this release instead of the newest')
    tool.add_argument('--github-output', type=Path, help='append archive=, version= and sha256= here')
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
            print('::notice title=Newest releases::This run built with the newest upstream releases; if '
                  'Convolver did not change, see the job summary for what to check.')
        return 0
    policy = load_policy(args.policy)
    try:
        if args.command == 'dependencies':
            records = resolve(policy, args.cache.resolve(), args.names or None, parse_wanted(args.version))
            args.lock.write_text(json.dumps(lock(records), indent=2) + '\n', encoding='utf-8')
            report(markdown(records))
        else:
            record, path = resolve_tool(policy, args.name, args.cache.resolve(), args.os_name, args.version)
            report(tool_markdown(args.name, record))
            if args.github_output:
                with args.github_output.open('a', encoding='utf-8') as output:
                    output.write(f'archive={path.as_posix()}\nversion={record["version"]}\nsha256={record["sha256"]}\n')
    except ResolveError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
