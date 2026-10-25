# SPDX-License-Identifier: GPL-2.0-or-later
"""Finding the newest upstream release of a dependency, with git alone.

Upstream tags are listed with `git ls-remote` (no API token or rate limit),
and a release's version is read from its tag: by the rule's `tag_prefix`
("v1.18.10" with prefix "v"), or by its `tag_pattern`, whose groups are joined
with dots (NSIS's "v313" is 3.13). See resolve.py for how a release becomes an
exact, recorded source.
"""
import re
import subprocess
import tempfile
import time

VERSION = re.compile(r'(?P<number>\d+(?:\.\d+)*)(?:[._-]build[._-](?P<build>\d+))?'
                     r'(?:[._-]?(?P<suffix>alpha|beta|rc|pre|pl)(?P<suffix_number>\d*))?$')
# Pre-releases sort below the release, by stage (alpha, then beta, then
# release candidate; "pre" is taken as a release candidate, as PEP 440 does),
# and patch levels (FFTW's -plN) above it. FINAL is the rank of a release.
FINAL = 3
RANK = {'alpha': 0, 'beta': 1, 'rc': 2, 'pre': 2, None: FINAL, 'pl': FINAL + 1}


def version_key(text):
    """Orders versions: numbers, then build, then alpha < beta < rc < release
    < patch level, then the stage's number (beta3 < rc1 < rc2)."""
    match = VERSION.fullmatch(text)
    if not match:
        return None
    numbers = tuple(int(part) for part in match['number'].split('.'))
    numbers += (0,) * (4 - len(numbers))
    build = int(match['build'] or 0)
    return numbers, build, RANK[match['suffix']], int(match['suffix_number'] or 0)


def is_final(text):
    """A release or patch level, not a pre-release."""
    key = version_key(text)
    return key is not None and key[2] >= FINAL


def parse_ls_remote(output):
    """Maps tag name to commit, preferring the peeled commit of an annotated tag."""
    tags = {}
    for line in output.splitlines():
        if '\t' not in line:
            continue
        commit, ref = line.split('\t', 1)
        if not ref.startswith('refs/tags/'):
            continue
        name = ref[len('refs/tags/'):]
        if name.endswith('^{}'):
            tags[name[:-3]] = commit
        else:
            tags.setdefault(name, commit)
    return tags


def _git(arguments, timeout=180, cwd=None):
    result = subprocess.run(['git', *arguments], capture_output=True, text=True, timeout=timeout,
                            check=False, cwd=cwd)
    if result.returncode:
        lines = result.stderr.strip().splitlines()
        raise RuntimeError(lines[-1] if lines else f'git {arguments[0]} exited with {result.returncode}')
    return result.stdout


def ls_remote(remote, attempts=3):
    """Lists a remote's tags, retrying a flaky host twice (after 5 s, then 10 s)."""
    for attempt in range(attempts):
        try:
            return _git(['ls-remote', '--tags', remote], timeout=120)
        except subprocess.TimeoutExpired as timeout:
            error = RuntimeError(f'no answer within {timeout.timeout} s')
        except RuntimeError as failure:
            error = failure
        if attempt + 1 < attempts:
            time.sleep(5 * 2**attempt)
    raise error


def tag_version(rule, tag):
    """The version a tag names under this rule, or None if it names no release."""
    pattern = rule.get('tag_pattern')
    if pattern:
        match = re.fullmatch(pattern, tag)
        version = '.'.join(group for group in match.groups() if group) if match else None
    else:
        prefix = rule.get('tag_prefix', '')
        version = tag[len(prefix):] if tag.startswith(prefix) else None
    return version if version and version_key(version) is not None else None


def releases(rule, tags):
    """{version: tag} for the tags that name a release; pre-releases only if the rule allows them."""
    found = {}
    for tag in tags:
        version = tag_version(rule, tag)
        if version and (is_final(version) or rule.get('prereleases', False)):
            found[version] = tag
    return found


def newest(rule, tags, wanted=None):
    """(version, tag, commit) of the newest release, or of `wanted` when given."""
    found = releases(rule, tags)
    if wanted is not None:
        if wanted not in found:
            raise ValueError(f'no release {wanted} among the tags of {rule["upstream"]} '
                             f'(releases found: {", ".join(sorted(found, key=version_key)[-5:]) or "none"})')
        version = wanted
    elif not found:
        raise ValueError(f'no release tags found at {rule["upstream"]} ({_describe_tags(rule)})')
    else:
        version = max(found, key=version_key)
    return version, found[version], tags[found[version]]


def newest_prerelease(rule, tags):
    """(version, tag, commit) of the newest pre-release (beta, rc) that is newer
    than the newest release, or None. Formal tests use releases; a pre-release
    is only tried, to see what it signposts."""
    final = releases(dict(rule, prereleases=False), tags)
    newest_final = max((version_key(v) for v in final), default=None)
    candidates = {}
    for tag in tags:
        version = tag_version(rule, tag)
        if version and not is_final(version) and (newest_final is None or version_key(version) > newest_final):
            candidates[version] = tag
    if not candidates:
        return None
    version = max(candidates, key=version_key)
    return version, candidates[version], tags[candidates[version]]


def _describe_tags(rule):
    if rule.get('tag_pattern'):
        return f'tags matching {rule["tag_pattern"]}'
    return f'tags "{rule.get("tag_prefix", "")}<version>"'


def gitlinks(remote, tag, paths):
    """The commits a tag's tree records for its submodules at `paths`, and the
    submodule URLs from its .gitmodules, without downloading file contents."""
    with tempfile.TemporaryDirectory(prefix='convolver-gitlinks-') as temporary:
        _git(['init', '--quiet', '--bare', temporary])
        _git(['remote', 'add', 'origin', remote], cwd=temporary)
        _git(['fetch', '--quiet', '--depth', '1', '--filter=blob:none', 'origin',
              f'refs/tags/{tag}:refs/tags/{tag}'], timeout=300, cwd=temporary)
        tree = _git(['ls-tree', f'refs/tags/{tag}', '--', *paths], cwd=temporary)
        modules = _git(['config', '--blob', f'refs/tags/{tag}:.gitmodules', '--get-regexp',
                        r'^submodule\..*\.(path|url)$'], cwd=temporary)
    commits = {}
    for line in tree.splitlines():
        meta, path = line.split('\t', 1)
        mode, kind, commit = meta.split()
        if mode == '160000' and kind == 'commit':
            commits[path] = commit
    names, urls = {}, {}
    for line in modules.splitlines():
        key, value = line.split(' ', 1)
        section, field = key.rsplit('.', 1)
        (names if field == 'path' else urls)[section] = value
    submodule_urls = {path: urls.get(section, '') for section, path in names.items()}
    return commits, submodule_urls


def same_repository(first, second):
    """True when two Git URLs name the same repository (ignoring .git and a final slash)."""
    def normal(url):
        url = url.strip().rstrip('/')
        return (url[:-4] if url.endswith('.git') else url).lower()
    return normal(first) == normal(second)
