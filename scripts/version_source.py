#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Commit and tag the next source version from a clean, full main checkout."""

import argparse
import re
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


def git(*args):
    return subprocess.run(['git', *args], check=True, text=True, capture_output=True).stdout.strip()


def version_tuple(value):
    if not re.fullmatch(VERSION_PATTERN, value):
        raise ValueError('VERSION must contain MAJOR.MINOR.PATCH without leading zeros')
    return tuple(map(int, value.split('.')))


def version_source(*, push=False):
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
    next_version = current if current > previous else (current[0], current[1], current[2] + 1)
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
    args = parser.parse_args()
    try:
        version_source(push=args.push)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        print('Versioning failed: ' + detail, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
