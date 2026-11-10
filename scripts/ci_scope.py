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
  need the candidate workflow, which runs the packaged guide's examples;
  a guide in docs/use/ needs it only if it has such an example or shows an
  image, before or after the change.
- one plug-in format's own files (CI-13): its adapter and its tests. These
  need that format's workflow. The macOS window that VST3 and the Audio Unit
  share (plugins/editor/mac/) counts as a format of its own here, needing
  both formats' workflows (CI-26).
- release files (CI-34): scripts/release/, tests/release/ and packaging/,
  apart from the few that other workflows use. Only the candidate workflow
  uses them.
- one workflow file: that workflow alone.
- everything else, including documentation that a build test reads or runs.
  Every workflow but the candidates runs.

The candidate workflow, the slowest (up to about 25 minutes), runs for a
pull request only when it changes what the packages are built from or hold
(CI-34): release files, packaged documentation, the macOS window, the build
configuration, the shipped dependencies and the other files in CANDIDATES.
For any other change it runs when started by hand on the branch.

Pushes to main, scheduled and manual runs always get everything, and so does
a pull request whose changed files cannot be listed. See
docs/develop/testing.md#ci-triggers.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

# Pages whose commands the examples.first_use CTest entry runs as written
# (tests/plugins/fixtures.py DOCUMENTED), and the archive and records that the
# documentation CTest entry checks (tests/regression/documentation_regressions.py),
# and the files kept from releases, which upgrade.cli hashes (COMPAT-1).
TESTED = (
    'README.md',
    'examples/first-use/README.md',
    'docs/use/first-filter.md',
    'docs/historical/',
    'docs/review-2026-10/archive-documentation.json',
    'docs/review-2026-10/sourceforge-documentation-check.json',
    'tests/upgrade/',
)
# Pages installed into the preview packages (cmake/ConvolverPluginPackaging.cmake).
PACKAGED = (
    'docs/use/',
    'examples/plugin-presets/README.md',
    'portable/third_party/ooura/README.md',
)
# The guides, docs/use/*.md. The candidates run their marked commands from a
# package (tests/release/documentation_examples.py), so a change to a guide
# builds them only when the page has such a command before or after the
# change (CI-34, jrp's choice of 8 November 2026). A page that shows an image
# builds them too, as only the packaging checks where its images come from
# (scripts/release/package_docs.py). Other prose changes are left to Lint,
# and main still packages every page.
# The pages of TESTED whose marked commands examples.first_use runs
# (tests/plugins/fixtures.py DOCUMENTED and MARKER). Only a change to those
# commands builds and tests everything; other edits to these pages count as
# what the page otherwise is (CI-35).
COMMANDED = ('README.md', 'examples/first-use/README.md', 'docs/use/first-filter.md')
FIRST_USE = re.compile(r'<!-- first-use-example -->\s*```sh\n(.*?)\n```', re.DOTALL)
GUIDES = 'docs/use/'
EXAMPLE = re.compile(r'<!-- (?:executable|refused)-example -->')
IMAGE = re.compile(r'!\[|<img\s')

# Files that only one plug-in format builds or tests (CI-13). Nothing outside
# these folders and the format's workflow may refer to them; the shared
# harness in tests/plugins/ and plugins/identities.json are everyone's.
FORMATS = {
    'vst3': ('plugins/vst3/', 'tests/vst3/'),
    'au': ('plugins/au/', 'tests/au/'),
    'lv2': ('plugins/lv2/', 'tests/lv2/'),
    # The macOS window: only the VST3 and Audio Unit builds compile it, and
    # their tests (tests/vst3/editor_tests.mm, tests/au/editor_tests.mm) test it.
    'editor': ('plugins/editor/mac/',),
}
# Files in a format's folders that other builds use too: they count as code.
SHARED = (
    'tests/vst3/fft_symbols.py',  # The LV2 tests run it on the LV2 bundle.
)
# The workflows that build and test each format; whether the candidates
# run is decided by candidates() below.
FORMAT_WORKFLOWS = {
    'vst3': {'vst3'},
    'au': {'au'},
    'lv2': {'lv2'},
    'editor': {'vst3', 'au'},
}
# Files only the candidate workflow uses (CI-34), apart from RELEASE_SHARED.
RELEASE = ('scripts/release/', 'tests/release/', 'packaging/')
# Release files that other workflows use too: they count as code.
RELEASE_SHARED = (
    # The VST3, Audio Unit and LV2 workflows, and the pluginval action,
    # resolve and fetch their SDKs and tools with these.
    'scripts/release/resolve.py',
    'scripts/release/sources.py',
    'scripts/release/upstream.py',  # resolve.py imports it.
    'scripts/release/contract.py',  # resolve.py and sources.py import it.
    'packaging/release-policy.json',  # contract.py reads it.
    'packaging/trademarks/vst.json',  # plugins/vst3/CMakeLists.txt reads it.
)
# Code for which a pull request still builds the candidates (CI-34): how the
# packages are configured and built, what they hold besides the packaged
# pages, and the shipped dependencies. All the release files (RELEASE),
# packaged pages, the macOS window and release.yml count too (candidates()). Other code
# builds the candidates on main, each week and when started by hand.
CANDIDATES = (
    'CMakeLists.txt',
    'cmake/',
    'plugins/vst3/CMakeLists.txt',
    'plugins/au/CMakeLists.txt',
    'VERSION',  # The packages' names.
    # Packaged or written into the bundles (cmake/ConvolverPluginPackaging.cmake).
    'COPYING',
    'examples/',
    'plugins/identities.json',
    'plugins/editor/',
    'plugins/vst3/Info.plist.in',
    'plugins/vst3/NOTICE.txt.in',
    'plugins/au/Info.plist.in',
    'plugins/au/NOTICE.txt',
    'plugins/au/GPL-3.0.txt',
    'tests/plugins/fixtures.py',  # Writes the packaged examples' WAV files.
    # The candidates' own checks run or compile these.
    'scripts/trademarks.py',  # tests/release/package_smoke.py
    'tests/au/bundle_tests.py',  # package_smoke.py and macos_installer.py
    'tests/vst3/package_probe.cpp',  # Built only with CONVOLVER_PACKAGE_PLUGINS.
    '.gitattributes',  # git archive writes the source archive with it.
    'portable/third_party/',  # Shipped dependency (Ooura), with its licence.
)
# Workflows gated by this script, named by their file in .github/workflows/.
WORKFLOWS = ('regression', 'fuzz', 'vst3', 'au', 'lv2', 'release')


def matches(path, entries):
    return any(path.startswith(entry) if entry.endswith('/') else path == entry for entry in entries)


def classify(path):
    """Return 'documentation', 'packaged', 'format:NAME', 'release',
    'workflow:NAME' or 'code' for a repository path."""
    if matches(path, TESTED):
        return 'code'
    if matches(path, PACKAGED):
        return 'packaged'
    if path.startswith('docs/') or PurePosixPath(path).suffix.lower() == '.md':
        return 'documentation'
    if path in SHARED or path in RELEASE_SHARED:
        return 'code'
    for name, folders in FORMATS.items():
        if matches(path, folders):
            return 'format:' + name
    if matches(path, RELEASE):
        return 'release'
    folder, _, file = path.rpartition('/')
    if folder == '.github/workflows' and file.endswith('.yml'):
        return 'workflow:' + file[: -len('.yml')]
    return 'code'


def versions(path):
    """The texts of a changed file before and after the change: the base
    commit's (HEAD^1, the merge commit's first parent) and the checked-out
    one's, each when it exists. None when neither exists, or when one that
    exists cannot be read as UTF-8."""
    texts = []
    try:
        texts.append(Path(path).read_text(encoding='utf-8'))
    except FileNotFoundError:
        pass
    except (OSError, UnicodeDecodeError):
        return None
    try:
        base = subprocess.run(['git', 'show', f'HEAD^1:{path}'], check=True, capture_output=True).stdout
    except subprocess.CalledProcessError:
        base = None  # Not in the base commit, or there is none.
    except OSError:
        return None
    if base is not None:
        try:
            texts.append(base.decode('utf-8'))
        except UnicodeDecodeError:
            return None
    return texts or None


def commands_changed(path):
    """Whether a page's marked first-use commands differ before and after
    the change. A page that is new, removed or cannot be read counts as
    changed."""
    texts = versions(path)
    if texts is None or len(texts) != 2:
        return True
    return FIRST_USE.findall(texts[0]) != FIRST_USE.findall(texts[1])


def page_kind(path):
    """classify(), except that a page in COMMANDED counts as code only when
    its marked commands change (CI-35)."""
    if path in COMMANDED and not commands_changed(path):
        return 'packaged' if matches(path, PACKAGED) else 'documentation'
    return classify(path)


def guide(path):
    """Whether a path is one of the guides, docs/use/*.md."""
    name = path[len(GUIDES) :]
    return path.startswith(GUIDES) and '/' not in name and name.endswith('.md')


def guide_needs_candidates(path):
    """Whether a guide has a command the candidates run, or shows an image,
    before or after the change. A page that cannot be read counts as one."""
    texts = versions(path)
    return texts is None or any(EXAMPLE.search(text) or IMAGE.search(text) for text in texts)


def candidates(paths):
    """Whether a pull request changing these paths builds the candidates."""
    for path in paths:
        what = page_kind(path)
        if what == 'documentation':
            continue
        if what == 'packaged' and guide(path):
            if guide_needs_candidates(path):
                return True
            continue
        if what in ('packaged', 'format:editor', 'workflow:release') or matches(path, RELEASE + CANDIDATES):
            return True
    return False


def needed(paths):
    """The workflows that must run for a pull request's changed paths. An
    empty list (a merge that changes nothing) runs everything."""
    if not paths:
        return set(WORKFLOWS)
    kinds = {page_kind(path) for path in paths}
    if 'code' in kinds:
        workflows = set(WORKFLOWS)
    else:
        workflows = set()
        for kind in kinds:
            if kind.startswith('format:'):
                workflows |= FORMAT_WORKFLOWS[kind.split(':', 1)[1]]
            elif kind.startswith('workflow:'):
                workflows.add(kind.split(':', 1)[1])
    workflows.discard('release')
    if candidates(paths):
        workflows.add('release')
    # Lint and the version workflow are not gated; changing them builds nothing.
    return workflows & set(WORKFLOWS)


def decide(paths, workflow=None):
    """What one workflow must do: build (and test), and package, which only
    the candidate workflow reads; for the candidates the two are the same.
    Without a workflow name, build says whether any other workflow builds."""
    workflows = needed(paths)
    package = 'release' in workflows
    build = bool(workflows - {'release'}) if workflow is None else workflow in workflows
    return {'build': build, 'package': package}


def changed_files():
    """Files a pull request changes, from the merge commit GitHub checks out.

    Its first parent is the base branch, so the diff is exactly what merging
    would change. Returns None when that cannot be worked out.
    """
    try:
        parents = subprocess.run(
            ['git', 'rev-list', '--parents', '-n', '1', 'HEAD'], check=True, capture_output=True, text=True
        ).stdout.split()
        if len(parents) != 3:
            return None
        output = subprocess.run(
            ['git', '-c', 'core.quotePath=false', 'diff', '--name-only', '--no-renames', 'HEAD^1', 'HEAD'],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return [line for line in output.splitlines() if line]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n', 1)[0])
    parser.add_argument(
        '--event',
        default=os.environ.get('GITHUB_EVENT_NAME', ''),
        help='GitHub event name (default: $GITHUB_EVENT_NAME)',
    )
    parser.add_argument(
        '--workflow', choices=WORKFLOWS, help='the workflow asking, by file name (default: any workflow)'
    )
    parser.add_argument('--github-output', help='append build=… and package=… here')
    parser.add_argument('paths', nargs='*', help='changed paths, instead of asking git')
    args = parser.parse_args(argv)

    if args.paths:
        paths, reason = args.paths, 'paths given on the command line'
    elif args.event != 'pull_request':
        paths, reason = None, f'"{args.event or "local"}" runs always test everything'
    else:
        paths = changed_files()
        reason = 'cannot list the changed files' if paths is None else f'{len(paths)} changed file(s)'
    answer = decide(paths, args.workflow) if paths is not None else {'build': True, 'package': True}

    lines = [f'CI scope: {reason}.', *(f'  {page_kind(path):13} {path}' for path in paths or [])]
    if paths is not None:
        workflows = needed(paths)
        if not workflows:
            lines.append('Only documentation changed: lint runs; builds and tests are skipped.')
        else:
            lines.append('These workflows build and test: ' + ', '.join(sorted(workflows)) + '.')
        if workflows and 'release' not in workflows:
            lines.append(
                'The candidates are not built: nothing that changed needs them (CI-34). '
                'To build them for this branch, start "Plug-in distribution candidates" by hand on it.'
            )
        if args.workflow:
            lines.append(
                f'This workflow ({args.workflow}) '
                + ('runs.' if answer['build'] or answer['package'] else 'is skipped.')
            )
    print('\n'.join(lines))
    if args.github_output:
        with Path(args.github_output).open('a', encoding='utf-8') as stream:
            stream.writelines(f'{key}={str(value).lower()}\n' for key, value in answer.items())
    return 0


if __name__ == '__main__':
    sys.exit(main())
