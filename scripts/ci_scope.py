#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Decide which CI work a pull request needs from the files it changes.

Each workflow that builds or packages starts with a small `scope` job that
runs this script with its own name (`--workflow`); its other jobs run only
when the answer says so. Lint runs for every change and is not gated. A
changed file is one of these kinds:

- documentation: Markdown anywhere and everything under docs/, unless listed
  below. Lint (pre-commit, including the link and repository checks) covers it.
  So nothing under docs/ may be built: benchmark programs live in
  tests/native/benchmarks/ and only their dated results in docs/benchmarks/.
- packaged documentation: pages copied into the preview packages. These also
  need the candidate workflow, which runs the packaged guide's examples.
- one plug-in format's own files (CI-13): its adapter and its tests. These
  need that format's workflow, and the VST3 ones the candidate workflow too.
- one workflow file: that workflow alone.
- everything else, including documentation that a build test reads or runs.
  Every workflow runs.

Pushes to main and manual runs always get everything, and so does a pull
request whose changed files cannot be listed. See
docs/develop/testing.md#documentation-only-pull-requests and
#pull-requests-for-one-plug-in-format.
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

# Files that only one plug-in format builds or tests (CI-13). Nothing outside
# these folders and the format's workflow may refer to them; the shared
# harness in tests/plugins/ and plugins/identities.json are everyone's.
FORMATS = {
    'vst3': ('plugins/vst3/', 'tests/vst3/'),
    'au': ('plugins/au/', 'tests/au/'),
    'lv2': ('plugins/lv2/', 'tests/lv2/'),
}
# Files in a format's folders that other builds use too: they count as code.
SHARED = (
    'tests/vst3/fft_symbols.py',  # The LV2 tests run it on the LV2 bundle.
)
# The workflows that build each format: the candidates build and package only
# the VST3 plug-in (scripts/release/candidate.py).
FORMAT_WORKFLOWS = {
    'vst3': {'vst3', 'release'},
    'au': {'au'},
    'lv2': {'lv2'},
}
# Workflows gated by this script, named by their file in .github/workflows/.
WORKFLOWS = ('regression', 'fuzz', 'vst3', 'au', 'lv2', 'release')


def matches(path, entries):
    return any(path.startswith(entry) if entry.endswith('/') else path == entry
               for entry in entries)


def classify(path):
    """Return 'documentation', 'packaged', 'format:NAME', 'workflow:NAME' or
    'code' for a repository path."""
    if matches(path, TESTED):
        return 'code'
    if matches(path, PACKAGED):
        return 'packaged'
    if path.startswith('docs/') or PurePosixPath(path).suffix.lower() == '.md':
        return 'documentation'
    if path in SHARED:
        return 'code'
    for name, folders in FORMATS.items():
        if matches(path, folders):
            return 'format:' + name
    folder, _, file = path.rpartition('/')
    if folder == '.github/workflows' and file.endswith('.yml'):
        return 'workflow:' + file[:-len('.yml')]
    return 'code'


def needed(paths):
    """The workflows that must build and test for a set of changed paths, or
    None for all of them."""
    kinds = {classify(path) for path in paths}
    if not paths or 'code' in kinds:
        return None
    workflows = set()
    for kind in kinds:
        if kind.startswith('format:'):
            workflows |= FORMAT_WORKFLOWS[kind.split(':', 1)[1]]
        elif kind.startswith('workflow:'):
            workflows.add(kind.split(':', 1)[1])
    # Lint and the version workflow are not gated; changing them builds nothing.
    return workflows & set(WORKFLOWS)


def decide(paths, workflow=None):
    """What one workflow (without a name, any workflow) must do: build (and
    test), and package, which only the candidate workflow reads. Packaged
    documentation needs the candidates even when nothing is built."""
    workflows = needed(paths)
    if workflows is None:
        return {'build': True, 'package': True}
    build = bool(workflows) if workflow is None else workflow in workflows
    packaged = any(classify(path) == 'packaged' for path in paths)
    return {'build': build, 'package': build or packaged}


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
        output = subprocess.run(['git', '-c', 'core.quotePath=false', 'diff', '--name-only', '--no-renames',
                                 'HEAD^1', 'HEAD'],
                                check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return [line for line in output.splitlines() if line]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n', 1)[0])
    parser.add_argument('--event', default=os.environ.get('GITHUB_EVENT_NAME', ''),
                        help='GitHub event name (default: $GITHUB_EVENT_NAME)')
    parser.add_argument('--workflow', choices=WORKFLOWS,
                        help='the workflow asking, by file name (default: any workflow)')
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
    answer = decide(paths, args.workflow) if paths is not None else {'build': True, 'package': True}

    lines = [f'CI scope: {reason}.']
    for path in paths or []:
        lines.append(f'  {classify(path):13} {path}')
    workflows = needed(paths) if paths is not None else None
    if workflows is not None and not workflows:
        lines.append('Only documentation changed: lint runs; builds and tests are skipped'
                     + ('; the packaged guide is checked by the candidate workflow.'
                        if answer['package'] else '.'))
    elif workflows is not None:
        lines.append("Only one plug-in format's or one workflow's own files changed (CI-13); "
                     'these workflows build and test: ' + ', '.join(sorted(workflows)) + '.')
        if args.workflow:
            lines.append(f'This workflow ({args.workflow}) '
                         + ('runs.' if answer['build'] or answer['package'] else 'is skipped.'))
    print('\n'.join(lines))
    if args.github_output:
        with open(args.github_output, 'a', encoding='utf-8') as stream:
            for key, value in answer.items():
                stream.write(f'{key}={str(value).lower()}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
