#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Commit and tag the next source version from a clean, full main checkout.

With --labels-from OWNER/NAME, a merged pull request labelled version:minor or
version:major (read with the GitHub CLI) makes the next version a minor or
major step instead of a patch (DOC-20, docs/develop/versioning.md).
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

VERSION_PATTERN = r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)'
# Keep the definition here rather than duplicating it in workflow path filters.
SOURCE_PATHS = (
    'VERSION',
    'CMakeLists.txt',
    'CMakePresets.json',
    'cmake',
    'include',
    'portable',
    'plugins',
    'historical',
    'tests',
    'scripts',
    '.github/workflows',
)
# Pull request labels that ask for a larger step than the automatic patch.
STEP_LABELS = {'version:minor': 1, 'version:major': 2}


def git(*args):
    return subprocess.run(['git', *args], check=True, text=True, capture_output=True).stdout.strip()


def merged_pull_request_labels(repository, commit):
    """Labels of the merged pull requests GitHub associates with a commit."""
    # which() honours PATHEXT, so a gh.cmd or gh.exe on Windows is found too.
    names = subprocess.run(
        [
            shutil.which('gh') or 'gh',
            'api',
            f'repos/{repository}/commits/{commit}/pulls',
            '--jq',
            '.[] | select(.merged_at != null) | .labels[].name',
        ],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    return {name.strip() for name in names.splitlines() if name.strip()}


def requested_step(repository, commits):
    """0 for a patch, 1 for a minor or 2 for a major step: the largest any
    of the commits' pull requests asks for with a label."""
    step = 0
    for commit in commits:
        for label in sorted(merged_pull_request_labels(repository, commit) & STEP_LABELS.keys()):
            print(f'{commit[:12]}: pull request labelled {label}')
            step = max(step, STEP_LABELS[label])
    return step


def version_tuple(value):
    if not re.fullmatch(VERSION_PATTERN, value):
        raise ValueError('VERSION must contain MAJOR.MINOR.PATCH without leading zeros')
    return tuple(map(int, value.split('.')))


def version_source(*, push=False, labels_from=None):
    if git('rev-parse', '--show-prefix'):
        raise ValueError('Run from the repository root')
    if git('branch', '--show-current') != 'main':
        raise ValueError('Source versioning requires the main branch')
    if git('rev-parse', '--is-shallow-repository') != 'false':
        raise ValueError('Source versioning requires full history and tags')
    if git('status', '--porcelain', '--untracked-files=all'):
        raise ValueError('Source versioning requires a clean working tree')
    if push:
        git('fetch', 'origin', 'main', '--tags')
        if git('rev-parse', 'HEAD') != git('rev-parse', 'refs/remotes/origin/main'):
            raise ValueError('main has moved; rerun the workflow from a fresh checkout')

    current = version_tuple(Path('VERSION').read_text(encoding='utf-8').strip())
    tags = {
        version_tuple(tag[1:]): tag
        for tag in git('tag', '--list', 'v*').splitlines()
        if re.fullmatch('v' + VERSION_PATTERN, tag)
    }
    reachable = set(git('tag', '--merged', 'HEAD').splitlines())
    previous = max((value for value, tag in tags.items() if tag in reachable), default=None)
    if previous is None:
        raise ValueError('No portable version baseline found; create an initial annotated vX.Y.Z tag')
    if current < previous:
        raise ValueError('VERSION must not go backwards from the latest source tag')
    changes = git('rev-list', '--first-parent', tags[previous] + '..HEAD', '--', *SOURCE_PATHS)
    if not changes:
        print('No unversioned source changes')
        return
    step = requested_step(labels_from, changes.splitlines()) if labels_from else 0
    stepped = (
        (previous[0], previous[1], previous[2] + 1),
        (previous[0], previous[1] + 1, 0),
        (previous[0] + 1, 0, 0),
    )[step]
    # An explicit VERSION edit still counts; a label never lowers it.
    next_version = max(current, stepped) if current > previous else stepped
    if next_version <= max(tags):
        raise ValueError('Next version conflicts with an existing tag; tags are never overwritten')
    version = '.'.join(map(str, next_version))
    tag = 'v' + version
    source = git('rev-parse', 'HEAD')
    if next_version != current:
        Path('VERSION').write_text(version + '\n', encoding='utf-8')
        git('add', '--', 'VERSION')
        git('commit', '-m', 'chore: version source as ' + tag, '--only', '--', 'VERSION')
    git('tag', '-a', tag, '-m', 'Convolver ' + version + '\n\nSource: ' + source)
    if push:
        # A racing main push rejects the entire update, including the tag.
        git('push', '--atomic', 'origin', 'HEAD:refs/heads/main', 'refs/tags/' + tag)
    print(tag + ' -> ' + git('rev-parse', 'HEAD'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--push', action='store_true', help='atomically push main and its new tag to origin')
    labels = parser.add_mutually_exclusive_group()
    labels.add_argument(
        '--labels-from',
        metavar='OWNER/NAME',
        help='read version:minor and version:major labels of merged pull requests with the GitHub CLI',
    )
    labels.add_argument(
        '--ignore-labels',
        action='store_true',
        help='version without reading labels; with --push, only when no merged pull request asks for a step',
    )
    args = parser.parse_args()
    try:
        if args.push and not (args.labels_from or args.ignore_labels):
            # A push without labels would publish a patch for a pull request
            # labelled for a minor or major step, and tags are never moved.
            raise ValueError(
                '--push needs --labels-from OWNER/NAME so that version:minor and version:major labels count '
                '(or --ignore-labels, when you have checked that no merged pull request has one)'
            )
        version_source(push=args.push, labels_from=args.labels_from)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        print('Versioning failed: ' + detail, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
