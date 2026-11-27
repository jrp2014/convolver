#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Delete the GitHub branches whose pull requests have merged (CI-56).

A dry run unless given --apply. Run it from a clone of the repository:

    python3 scripts/maintenance/delete_merged_branches.py            # look
    python3 scripts/maintenance/delete_merged_branches.py --apply    # delete

A branch is deleted only when a merged pull request recorded its current
commit as the pull request's head and no pull request from it is open, so
nothing is lost: GitHub keeps that commit as refs/pull/N/head and shows
"Restore branch" on the pull request. A branch whose closed, unmerged pull
request recorded its current commit is deleted only with --closed. Every
other branch is kept and listed: the default branch, open pull requests,
commits pushed after the last pull request, and branches with none.

Pull requests are read from GitHub's public API (GITHUB_TOKEN or GH_TOKEN is
used when set, for a higher rate limit); branches are deleted with git push,
using your own credentials, and only if each is still at the commit checked
(--force-with-lease). Each deletion and its restore command are printed and
appended to .git/deleted-branches.log.
"""

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

API = 'https://api.github.com'
MAX_PAGES = 50


def git(*args, check=True):
    result = subprocess.run(['git', *args], text=True, capture_output=True, check=False)
    if check and result.returncode != 0:
        raise SystemExit(f'git {" ".join(args)} failed: {result.stderr.strip()}')
    return result


def repo_slug(url):
    """Return 'owner/name' for a GitHub remote URL, or None."""
    match = re.search(r'github\.com[:/]+([^/]+)/([^/]+?)(?:\.git)?/?$', url.strip())
    return f'{match.group(1)}/{match.group(2)}' if match else None


def remote_branches():
    out = git('for-each-ref', '--format=%(refname:strip=3) %(objectname)', 'refs/remotes/origin').stdout
    branches = {}
    for line in out.splitlines():
        name, sha = line.split(' ', 1)
        if name != 'HEAD':
            branches[name] = sha
    return branches


def api(path):
    token = os.environ.get('GITHUB_TOKEN') or os.environ.get('GH_TOKEN')
    request = urllib.request.Request(f'{API}{path}', headers={'Accept': 'application/vnd.github+json'})  # noqa: S310
    if token:
        request.add_header('Authorization', f'Bearer {token}')
    try:
        with urllib.request.urlopen(request, timeout=30) as response:  # noqa: S310
            return json.load(response)
    except urllib.error.HTTPError as error:
        hint = ' (rate limit: set GITHUB_TOKEN or wait an hour)' if error.code in (403, 429) else ''
        raise SystemExit(f'GitHub answered {error.code} for {path}{hint}; nothing deleted') from None
    except OSError as error:
        raise SystemExit(f'could not reach GitHub ({error}); nothing deleted') from None


def default_branch(slug):
    return api(f'/repos/{slug}')['default_branch']


def fetch_pulls(slug):
    """Every pull request of the repository, newest first."""
    pulls = []
    for page in range(1, MAX_PAGES + 1):
        batch = api(f'/repos/{slug}/pulls?state=all&sort=created&direction=desc&per_page=100&page={page}')
        pulls.extend(batch)
        if len(batch) < 100:
            return pulls
    raise SystemExit(f'more than {MAX_PAGES * 100} pull requests; raise MAX_PAGES')


def classify(branches, pulls, slug, protected):
    """Decide each branch: ('delete' | 'closed' | 'keep', pull request number or None, reason)."""
    by_branch = {}
    # A branch an open pull request merges into: deleting it would close that pull request.
    bases = {}
    for pull in pulls:
        if pull.get('state') == 'open':
            bases.setdefault((pull.get('base') or {}).get('ref'), pull['number'])
    for pull in pulls:
        head = pull.get('head') or {}
        repo = head.get('repo') or {}
        if repo.get('full_name') == slug:
            by_branch.setdefault(head.get('ref'), []).append(pull)
    verdicts = {}
    for name, sha in sorted(branches.items()):
        own = sorted(by_branch.get(name, []), key=lambda p: p['number'], reverse=True)
        open_pulls = [p['number'] for p in own if p.get('state') == 'open']
        at_tip = [p for p in own if (p.get('head') or {}).get('sha') == sha]
        merged = [p for p in at_tip if p.get('merged_at')]
        closed = [p for p in at_tip if p.get('state') == 'closed' and not p.get('merged_at')]
        if name in protected:
            verdicts[name] = ('keep', None, 'the default branch')
        elif open_pulls:
            verdicts[name] = ('keep', open_pulls[0], f'open pull request #{open_pulls[0]}')
        elif name in bases:
            verdicts[name] = ('keep', bases[name], f'the base of open pull request #{bases[name]}')
        elif merged:
            verdicts[name] = ('delete', merged[0]['number'], f'merged in #{merged[0]["number"]}')
        elif closed:
            number = closed[0]['number']
            verdicts[name] = ('closed', number, f'#{number} closed without merging')
        elif own:
            verdicts[name] = ('keep', own[0]['number'], f'has commits pushed after #{own[0]["number"]}')
        else:
            verdicts[name] = ('keep', None, 'no pull request')
    return verdicts


def delete(name, sha, number, log):
    pushed = git(
        'push', '--quiet', 'origin', f'--force-with-lease=refs/heads/{name}:{sha}', f':refs/heads/{name}', check=False
    )
    if pushed.returncode != 0:
        print(f'  SKIPPED {name}: it moved since the check, or the push was refused')
        return False
    restore = f'git fetch origin pull/{number}/head && git push origin {sha}:refs/heads/{name}'
    print(f'  deleted {name} at {sha[:9]} (#{number})\n    restore: {restore}')
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    with log.open('a', encoding='utf-8') as out:
        out.write(f'{stamp} {name} {sha} #{number}  restore: {restore}\n')
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n', 1)[0])
    parser.add_argument('--apply', action='store_true', help='delete the branches (default: only list them)')
    parser.add_argument('--closed', action='store_true', help='also delete branches of closed, unmerged pull requests')
    args = parser.parse_args(argv)

    top = git('rev-parse', '--show-toplevel', check=False)
    if top.returncode != 0:
        raise SystemExit('run this inside a clone of the repository')
    os.chdir(top.stdout.strip())
    slug = repo_slug(git('remote', 'get-url', 'origin').stdout)
    if not slug:
        raise SystemExit('origin is not a GitHub repository')
    git('fetch', '--quiet', '--prune', 'origin')
    branches = remote_branches()
    verdicts = classify(branches, fetch_pulls(slug), slug, {default_branch(slug)})

    groups = (
        ('delete', 'Merged, at the head their pull request recorded:'),
        ('closed', 'Closed without merging (deleted only with --closed):'),
        ('keep', 'Kept:'),
    )
    for kind, title in groups:
        names = [n for n, v in verdicts.items() if v[0] == kind]
        if names:
            print(f'\n{title}')
            for name in names:
                print(f'  {name:48} {branches[name][:9]}  {verdicts[name][2]}')

    targets = [n for n, v in verdicts.items() if v[0] == 'delete' or (args.closed and v[0] == 'closed')]
    print()
    if not targets:
        print('Nothing to delete.')
    elif not args.apply:
        print(f'Dry run: nothing deleted. Add --apply to delete {len(targets)} branch(es).')
    else:
        log = Path(git('rev-parse', '--git-common-dir').stdout.strip()).resolve() / 'deleted-branches.log'
        print(f'Deleting {len(targets)} branch(es):')
        done = sum(delete(n, branches[n], verdicts[n][1], log) for n in targets)
        print(f'\nDeleted {done}; restore commands appended to {log}')
        git('fetch', '--quiet', '--prune', 'origin')

    gone = [line for line in git('branch', '-vv').stdout.splitlines() if ': gone]' in line]
    if gone:
        print('\nLocal branches whose GitHub branch is gone (scripts/maintenance/workspace.sh tidy removes them):')
        print('\n'.join(gone))
    return 0


if __name__ == '__main__':
    sys.exit(main())
