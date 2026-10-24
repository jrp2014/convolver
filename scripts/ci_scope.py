#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Decide which CI work a pull request needs from the files it changes.

Each workflow that builds or packages starts with a small `scope` job that
runs this script; its other jobs run only when the answer says so. Lint runs
for every change and is not gated. A change is one of three kinds:

- documentation: Markdown anywhere and everything under docs/, unless listed
  below. Lint (pre-commit, including the link and repository checks) covers it.
- packaged documentation: pages copied into the preview packages. These also
  need the candidate workflow, which runs the packaged guide's examples.
- everything else, including documentation that a build test reads or runs.

Pushes to main and manual runs always get everything, and so does a pull
request whose changed files cannot be listed. See
docs/develop/testing.md#ci-triggers.
"""
import argparse
import os
from pathlib import PurePosixPath
import subprocess
import sys

# Pages whose commands the examples.first_use CTest entry runs as written
# (tests/plugins/fixtures.py DOCUMENTED), and the archive and records that the
# documentation CTest entry checks (tests/regression/documentation_regressions.py).
TESTED = (
    'README.md',
    'examples/first-use/README.md',
    'docs/use/first-filter.md',
    'docs/historical/',
    'docs/review-2026-10/archive-documentation.json',
    'docs/review-2026-10/sourceforge-documentation-check.json',
)
# Pages installed into the preview packages (cmake/ConvolverPluginPackaging.cmake).
PACKAGED = (
    'docs/use/',
    'examples/plugin-presets/README.md',
    'portable/third_party/ooura/README.md',
)


def matches(path, entries):
    return any(path.startswith(entry) if entry.endswith('/') else path == entry
               for entry in entries)


def classify(path):
    """Return 'documentation', 'packaged' or 'code' for a repository path."""
    if matches(path, TESTED):
        return 'code'
    if matches(path, PACKAGED):
        return 'packaged'
    if path.startswith('docs/') or PurePosixPath(path).suffix.lower() == '.md':
        return 'documentation'
    return 'code'


def decide(paths):
    """The jobs a set of changed paths needs: build (and test) and package."""
    kinds = {classify(path) for path in paths}
    build = 'code' in kinds or not paths
    return {'build': build, 'package': build or 'packaged' in kinds}


def changed_files():
    """Files a pull request changes, from the merge commit GitHub checks out.

    Its first parent is the base branch, so the diff is exactly what merging
    would change. Returns None when that cannot be worked out.
    """
    try:
        parents = subprocess.run(['git', 'rev-list', '--parents', '-n', '1', 'HEAD'],
                                 check=True, capture_output=True, text=True).stdout.split()
        if len(parents) != 3:
            return None
        output = subprocess.run(['git', 'diff', '--name-only', '--no-renames', 'HEAD^1', 'HEAD'],
                                check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return [line for line in output.splitlines() if line]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n', 1)[0])
    parser.add_argument('--event', default=os.environ.get('GITHUB_EVENT_NAME', ''),
                        help='GitHub event name (default: $GITHUB_EVENT_NAME)')
    parser.add_argument('--github-output', help='append build=… and package=… here')
    parser.add_argument('paths', nargs='*', help='changed paths, instead of asking git')
    args = parser.parse_args(argv)

    if args.paths:
        paths, reason = args.paths, 'paths given on the command line'
    elif args.event != 'pull_request':
        paths, reason = None, f'"{args.event or "local"}" runs always test everything'
    else:
        paths = changed_files()
        reason = ('cannot list the changed files' if paths is None
                  else f'{len(paths)} changed file(s)')
    answer = decide(paths) if paths is not None else {'build': True, 'package': True}

    lines = [f'CI scope: {reason}.']
    for path in paths or []:
        lines.append(f'  {classify(path):13} {path}')
    if not answer['build']:
        lines.append('Only documentation changed: lint runs; builds and tests are skipped'
                     + ('; the packaged guide is checked by the candidate workflow.'
                        if answer['package'] else '.'))
    print('\n'.join(lines))
    if args.github_output:
        with open(args.github_output, 'a', encoding='utf-8') as stream:
            for key, value in answer.items():
                stream.write(f'{key}={str(value).lower()}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
