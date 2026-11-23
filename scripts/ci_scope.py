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
- upgrade files (CI-42): tests/upgrade/, the files kept from published
  releases (Markdown included, as the version folders hash it) and the
  programs that check them. upgrade.cli, vst3.upgrade, lv2.upgrade and the
  Audio Unit workflow's state step read them, so they need the native, VST3,
  LV2 and Audio Unit workflows.
- manual host scripts (HOST-11): scripts that jrp runs by hand on his Mac,
  and their tests. No workflow builds or runs them; lint's pre-commit runs
  their tests, so they need no other workflow.
- record checks (DOC-20): the repository and link checks and the change
  fragment folder, and their tests. Only lint's pre-commit runs them, so they
  need no other workflow.
- versioning (DOC-20): the version helper and its tests, which the native
  workflow's CTest entry `versioning` runs (and pre-commit, and the version
  workflow on main). They need the native workflow only.
- one workflow file: that workflow alone.
- everything else, including documentation that a build test reads or runs.
  Every workflow but the candidates and the fuzz targets runs.

The candidate workflow, the slowest (up to about 25 minutes), runs for a
pull request only when it changes what the packages are built from or hold
(CI-34): release files, packaged documentation, the macOS window, the build
configuration, the shipped dependencies and the other files in CANDIDATES.
For any other change it runs when started by hand on the branch.

The fuzz workflow runs for a pull request only when it changes what the
fuzz targets are built from (CI-34): their harnesses, seeds and regression
inputs, the production sources compiled into them (the parsers, the state
decoder, the audio reader and the code they call), the build configuration
and the files the workflow runs (FUZZED). The workflow checks, on every run,
that FUZZED names every repository file compiled into the targets
(--fuzz-sources), so a new dependency is caught on the pull request that
adds it, which has to change a file already listed.

A later push carries a passed result forward (CI-53): each workflow looks up
its own last full run that passed on the same branch (a run whose scope job
reached the step PROVING_STEP) and compares that commit with this one. When
nothing changed since then that this workflow needs, it skips its jobs and
says which run it relies on. Without such a run, when the commits have
diverged, when the comparison cannot be listed, or when a person re-runs a
run, nothing is carried. A pull request run tests the branch merged into
main as it was then (jrp, 23 November 2026): a full pull request run records
the main commit it was merged into as a notice titled BASE_TITLE on its
scope job, and when main has moved since, a later run counts what changed
on main between that commit and the first parent of its own merge as
changed too (but for the version bot's VERSION commits). Without that
record, nothing is carried. On a pull request the comparison decides for
each workflow; on main every workflow runs as before when anything but
documentation changed, so a merge that changes code or tests gets the full
run (jrp, 21 November 2026), and one that changes only documentation runs
Lint alone (main_carries()).

A draft pull request runs Lint and the Linux jobs only (CI-54): the scope
job answers full=false, the macOS and Windows jobs are gated on full, and
the matrices that mix platforms are listed in the scope job (--matrix) and
filtered here. Marking the pull request ready runs the rest; a person or
agent who wants every platform sooner starts the workflows by hand on the
branch.

Pushes to main that change code, scheduled and manual runs get everything,
and so does a pull request whose changed files cannot be listed. See
docs/develop/testing.md#ci-triggers.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request
from pathlib import Path, PurePosixPath

# Pages whose commands the examples.first_use CTest entry runs as written
# (tests/plugins/fixtures.py DOCUMENTED), and the archive and records that the
# documentation CTest entry checks (tests/regression/documentation_regressions.py).
# The files kept from releases, which upgrade.cli hashes, are UPGRADE below.
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
# The files kept from published releases and the programs that check them
# (COMPAT-1, CI-42). Three CTest entries read them: upgrade.cli, which the
# root CMakeLists.txt registers wherever the CLI is built; vst3.upgrade, which
# plugins/vst3/CMakeLists.txt registers with convolver_vst3_render
# (tests/upgrade/vst3_render.cpp); and lv2.upgrade, which
# plugins/lv2/CMakeLists.txt registers with convolver_lv2_render
# (tests/upgrade/lv2_render.cpp). The native workflow runs upgrade.cli in
# every job, the VST3 workflow runs it and vst3.upgrade, and the LV2
# workflow's sanitizer job runs lv2.upgrade. The Audio Unit workflow's
# Release job runs upgrade.cli in its suite and restores the Audio Unit state
# (run_upgrade.py --au-render) through AVAudioEngine (COMPAT-1). Since CI-43 the VST3 macOS job's
# suite runs upgrade.cli with the same compiler (the newest Xcode on macos-26, which
# macos-latest also is today), and every macOS run checks the same bound.
UPGRADE = ('tests/upgrade/',)
UPGRADE_WORKFLOWS = {'regression', 'vst3', 'lv2', 'au'}
# The headless Ardour test (HOST-9), which only the VST3 and LV2 workflows
# run. It sits in the shared tests/plugins/ because both use it, but it is
# no other build's code; the fixtures it imports count as code as usual.
ARDOUR = (
    'tests/plugins/ardour_harness_tests.py',
    'tests/plugins/ardour_reopen.lua',
    'tests/plugins/ardour_session.lua',
    'tests/plugins/ardour_tests.py',
)
ARDOUR_WORKFLOWS = {'vst3', 'lv2'}
# Host scripts jrp runs by hand (HOST-11), and their tests, which pre-commit
# runs. No workflow builds or runs them.
MANUAL = ('scripts/reaper_check.py', 'tests/reaper_check_tests.py')
# The record checks and the fragment folder (DOC-20), and their tests: only
# lint's pre-commit runs them, so they build nothing.
RECORD_CHECKS = (
    'scripts/check_repository.py',
    'scripts/check_doc_links.py',
    'scripts/fold_changes.py',
    'tests/repository_checks_tests.py',
    'tests/documentation_links_tests.py',
    'tests/fold_changes_tests.py',
)
# The version helper and its tests (DOC-20): the CTest entry `versioning`
# (root CMakeLists.txt) runs the tests, which the native workflow covers;
# pre-commit and the version workflow on main run them too. No build reads
# the helper: CMake reads VERSION itself.
VERSIONING = ('scripts/version_source.py', 'tests/versioning_tests.py')
VERSIONING_WORKFLOWS = {'regression'}
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
    'plugins/lv2/CMakeLists.txt',  # LV2-5: the Linux package's LV2 bundle.
    'plugins/lv2/generate_ttl.py',  # Writes its Turtle files.
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
    '.github/actions/newest-compiler/',  # Chooses the compiler that builds them (CI-43).
    'portable/third_party/',  # Shipped dependency (Ooura), with its licence.
)
# What the fuzz targets are built from (CI-34): for a pull request, the fuzz
# workflow runs only when one of these changes, or the workflow itself. The
# sources are those compiled into the four targets on the Linux runner, as
# `llvm-dwarfdump --show-sources` lists them; the workflow checks that list
# against this one on every run (fuzz_sources()). Markdown here is still
# documentation.
FUZZED = (
    'tests/fuzz/',  # Harnesses, seeds, regression inputs and the scheduler's tests.
    'scripts/fuzz_campaigns.sh',  # Runs the campaigns.
    'scripts/ci_scope.py',  # The workflow checks its sources with it.
    '.github/actions/use-every-cpu/',  # The workflow uses it.
    '.github/actions/newest-compiler/',  # Chooses the Clang that builds them (CI-43).
    # Build options, compiler flags and the FFTW and libsndfile lookups.
    'CMakeLists.txt',
    'cmake/',
    # Compiled into the targets. The configuration parsers and the audio
    # reader (config_text, config_list, ir_file):
    'portable/config.cpp',
    'portable/config_parse.cpp',
    'portable/config_parse.hpp',
    'portable/sound_files.cpp',
    'portable/sound_files.hpp',
    'portable/io.cpp',
    'include/convolver/io.hpp',
    'portable/utf8.hpp',
    # The state decoder (host_state):
    'portable/state.cpp',
    'portable/bytes.hpp',
    'include/convolver/host.hpp',
    'include/convolver/advice.hpp',
    'portable/advice.hpp',
    # The engine, which holds validate_configuration() that the state decoder
    # calls, and what its object file brings with it:
    'include/convolver/engine.hpp',
    'portable/engine.cpp',
    'portable/partitions.cpp',
    'portable/partitions.hpp',
    'portable/kernels.cpp',
    'portable/kernels.hpp',
    'portable/kernels.inc',
    'portable/kernels_avx2.cpp',
    'portable/fft.hpp',
    'portable/fft_fftw.cpp',  # The runner's FFT backend.
    'portable/denormals.hpp',
)
# Files the build writes that may be compiled into the fuzz targets, by name,
# once the files each is made from are in FUZZED. None is today; the check
# fails on any other written file it finds (CI-34).
FUZZED_GENERATED = ()
# Workflows gated by this script, named by their file in .github/workflows/.
WORKFLOWS = ('regression', 'fuzz', 'vst3', 'au', 'lv2', 'release')
# The scope job's step that runs only when the workflow's jobs ran in full:
# built (or packaged), on every platform and not carried forward. Later
# pushes compare with the commit of the newest passed run that reached it
# (CI-53); the name is how they find it, so the workflows use it exactly.
PROVING_STEP = 'Full run: later pushes compare with this commit'
# Runs inspected for one, newest first, and the most files a comparison lists.
CARRY_RUNS = 5
# A full pull request run records the main commit it tested against as a
# notice with this title on its scope job (CI-53).
BASE_TITLE = 'CI-53 base'
COMPARE_LIMIT = 300


def matches(path, entries):
    return any(path.startswith(entry) if entry.endswith('/') else path == entry for entry in entries)


def classify(path):
    """Return 'documentation', 'packaged', 'format:NAME', 'release',
    'upgrade', 'ardour', 'manual', 'records', 'versioning', 'workflow:NAME'
    or 'code' for a repository path."""
    if matches(path, UPGRADE):
        return 'upgrade'
    if matches(path, ARDOUR):
        return 'ardour'
    if path in MANUAL:
        return 'manual'
    if path in RECORD_CHECKS:
        return 'records'
    if path in VERSIONING:
        return 'versioning'
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


def carried_kind(path):
    """page_kind() for a file changed since an earlier run (CI-53). A page in
    COMMANDED counts as code, as page_kind() compares its commands with the
    base commit, not with the earlier run's."""
    return classify(path) if path in COMMANDED else page_kind(path)


def candidates(paths, kind_of=page_kind):
    """Whether a pull request changing these paths builds the candidates."""
    for path in paths:
        what = kind_of(path)
        if what == 'documentation':
            continue
        if what == 'packaged' and guide(path):
            if guide_needs_candidates(path):
                return True
            continue
        if what in ('packaged', 'format:editor', 'workflow:release') or matches(path, RELEASE + CANDIDATES):
            return True
    return False


def fuzzed(paths, kind_of=page_kind):
    """Whether a pull request changing these paths runs the fuzz targets."""
    return any(matches(path, FUZZED) and kind_of(path) == 'code' for path in paths)


def needed(paths, kind_of=page_kind):
    """The workflows that must run for a pull request's changed paths. An
    empty list (a merge that changes nothing) runs everything."""
    if not paths:
        return set(WORKFLOWS)
    kinds = {kind_of(path) for path in paths}
    if 'code' in kinds:
        workflows = set(WORKFLOWS)
    else:
        workflows = set()
        for kind in kinds:
            if kind.startswith('format:'):
                workflows |= FORMAT_WORKFLOWS[kind.split(':', 1)[1]]
            elif kind == 'upgrade':
                workflows |= UPGRADE_WORKFLOWS
            elif kind == 'ardour':
                workflows |= ARDOUR_WORKFLOWS
            elif kind == 'versioning':
                workflows |= VERSIONING_WORKFLOWS
            elif kind.startswith('workflow:'):
                workflows.add(kind.split(':', 1)[1])
    workflows.discard('release')
    if candidates(paths, kind_of):
        workflows.add('release')
    if 'workflow:fuzz' not in kinds and not fuzzed(paths, kind_of):
        workflows.discard('fuzz')
    # Lint and the version workflow are not gated; changing them builds nothing.
    return workflows & set(WORKFLOWS)


def decide(paths, workflow=None, kind_of=page_kind):
    """What one workflow must do: build (and test), and package, which only
    the candidate workflow reads; for the candidates the two are the same.
    Without a workflow name, build says whether any other workflow builds."""
    workflows = needed(paths, kind_of)
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


def fuzz_sources(lines, root):
    """Check the source paths compiled into the fuzz targets (one per line,
    as llvm-dwarfdump --show-sources prints them) against FUZZED. Returns the
    repository files FUZZED does not name, the files inside the repository
    that are not tracked (written by the build, such as a generated header)
    and not in FUZZED_GENERATED, and the harnesses (tests/fuzz/fuzz_*.cpp)
    that are missing from the list. Paths outside the repository, and names
    that are not files (such as <stdin>), are left out."""
    root = Path(root).resolve()
    tracked = set(
        subprocess.run(
            ['git', '-c', 'core.quotePath=false', 'ls-files'], cwd=root, check=True, capture_output=True, text=True
        ).stdout.splitlines()
    )
    sources, generated = set(), set()
    for line in lines:
        name = line.strip()
        if not name:
            continue
        path = Path(name)
        path = (path if path.is_absolute() else root / path).resolve()
        try:
            relative = path.relative_to(root).as_posix()
        except ValueError:
            continue  # Outside the repository.
        if relative in tracked:
            sources.add(relative)
        elif path.is_file() and path.name not in FUZZED_GENERATED:
            generated.add(relative)
    harnesses = {path for path in tracked if re.fullmatch(r'tests/fuzz/fuzz_[^/]*\.cpp', path)}
    missing = sorted(path for path in sources if not matches(path, FUZZED))
    return missing, sorted(generated), sorted(harnesses - sources)


def check_fuzz_sources(source):
    """--fuzz-sources: fail when a repository file compiled into the fuzz
    targets is missing from FUZZED, when a file the build wrote is compiled
    in, or when a harness is missing from the list (it did not come from all
    the targets)."""
    lines = sys.stdin.readlines() if source == '-' else Path(source).read_text(encoding='utf-8').splitlines()
    missing, generated, absent = fuzz_sources(lines, Path(__file__).resolve().parents[1])
    failed = False
    if absent:
        print(
            'CI scope: these harnesses are not among the sources; were they read from all the fuzz targets?',
            *(f'  {path}' for path in absent),
            sep='\n',
            file=sys.stderr,
        )
        failed = True
    if missing:
        print(
            'CI scope: these files are compiled into the fuzz targets, but a pull request that changes them\n'
            'would not run the fuzz workflow. Add them to FUZZED in scripts/ci_scope.py (CI-34):',
            *(f'  {path}' for path in missing),
            sep='\n',
            file=sys.stderr,
        )
        failed = True
    if generated:
        print(
            'CI scope: these files, written by the build, are compiled into the fuzz targets. Add the files\n'
            'each is made from to FUZZED, and its name to FUZZED_GENERATED, in scripts/ci_scope.py (CI-34):',
            *(f'  {path}' for path in generated),
            sep='\n',
            file=sys.stderr,
        )
        failed = True
    if failed:
        return 1
    print('CI scope: FUZZED names every repository file compiled into the fuzz targets.')
    return 0


def runs(answer):
    """Whether a workflow's own jobs run, given decide() for that workflow.

    Only build: package says whether the candidates are needed, which only
    the candidate workflow reads, and for that workflow build is the same."""
    return answer['build']


def event_payload():
    """The event that started the run ($GITHUB_EVENT_PATH), or {}."""
    path = os.environ.get('GITHUB_EVENT_PATH')
    if not path:
        return {}
    try:
        return json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return {}


def full_run(event, payload):
    """Whether every platform runs: all but a draft pull request (CI-54)."""
    return not (event == 'pull_request' and payload.get('pull_request', {}).get('draft'))


def github(path):
    """A GitHub REST API answer, read with the run's token."""
    url = os.environ.get('GITHUB_API_URL', 'https://api.github.com') + path
    request = urllib.request.Request(  # noqa: S310 (GitHub's API URL, https://)
        url,
        headers={
            'Accept': 'application/vnd.github+json',
            'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'],
            'X-GitHub-Api-Version': '2022-11-28',
        },
    )
    with urllib.request.urlopen(request, timeout=30) as response:  # noqa: S310
        return json.load(response)


def proving(run, repository):
    """A run's scope job if it reached PROVING_STEP, else None."""
    jobs = github(f'/repos/{repository}/actions/runs/{run["id"]}/jobs?filter=latest&per_page=100')['jobs']
    for job in jobs:
        if job.get('name') == 'scope':
            reached = any(
                step.get('name') == PROVING_STEP and step.get('conclusion') == 'success'
                for step in job.get('steps') or []
            )
            return job if reached else None
    return None


def tested_base(job, repository):
    """The main commit a full pull request run tested against, from the
    BASE_TITLE notice on its scope job, or None."""
    notes = github(f'/repos/{repository}/check-runs/{job["id"]}/annotations?per_page=100')
    return next((note.get('message', '').strip() for note in notes if note.get('title') == BASE_TITLE), None)


def pull_request_base():
    """The main commit this pull request run tests against: the first parent
    of the merge commit GitHub checks out, or None."""
    try:
        parents = subprocess.run(
            ['git', 'rev-list', '--parents', '-n', '1', 'HEAD'], check=True, capture_output=True, text=True
        ).stdout.split()
    except (OSError, subprocess.CalledProcessError):
        return None
    return parents[1] if len(parents) == 3 else None


def carried_from(workflow, event, payload):
    """The files changed since this workflow's last full passed run on the
    same branch, and that run: (paths, run), or (None, why not)."""
    repository = os.environ.get('GITHUB_REPOSITORY', '')
    if not os.environ.get('GITHUB_TOKEN') or not repository:
        return None, 'no token to look up earlier runs'
    if os.environ.get('GITHUB_RUN_ATTEMPT', '1') != '1':
        return None, 'a person re-ran this run'
    if event == 'pull_request':
        pull = payload.get('pull_request') or {}
        head = pull.get('head') or {}
        if (head.get('repo') or {}).get('full_name') != repository:
            return None, 'the pull request comes from another repository'
        branch, sha = head.get('ref'), head.get('sha')
        merged_into = pull_request_base()
        if not merged_into:
            return None, 'the main commit this run tests against is unknown'
    elif event == 'push' and os.environ.get('GITHUB_REF') == 'refs/heads/main':
        branch, sha = 'main', os.environ.get('GITHUB_SHA')
    else:
        return None, f'"{event}" runs carry nothing'
    if not branch or not sha:
        return None, 'the event names no branch or commit'
    query = urllib.parse.urlencode({'event': event, 'branch': branch, 'status': 'success', 'per_page': CARRY_RUNS})
    try:
        found = github(f'/repos/{repository}/actions/workflows/{workflow}.yml/runs?{query}')['workflow_runs']
        current = os.environ.get('GITHUB_RUN_ID')
        base, job = next(
            (
                (run, job)
                for run in found
                if str(run['id']) != current and (run.get('head_repository') or {}).get('full_name') == repository
                for job in [proving(run, repository)]
                if job
            ),
            (None, None),
        )
        if base is None:
            return None, f'no full passed run of this workflow on {branch} to compare with'
        tested = tested_base(job, repository) if event == 'pull_request' else None
        if event == 'pull_request' and not tested:
            return None, f'run {base["id"]} recorded no main commit that it tested against'
        paths = changed_between(repository, base['head_sha'], sha)
        if isinstance(paths, str):
            return None, paths
        if tested and tested != merged_into:
            # main moved since: what landed on it counts as changed too,
            # except the version bot's VERSION commits (as in main_carries()).
            on_main = changed_between(repository, tested, merged_into)
            if isinstance(on_main, str):
                return None, f'on main since run {base["id"]}, {on_main}'
            paths |= on_main - {'VERSION'}
    except (OSError, KeyError, ValueError) as error:
        return None, f'could not look up earlier runs ({error})'
    return sorted(paths), base


def changed_between(repository, old, new):
    """The files changed from commit old to commit new (both names of a
    rename), or why they cannot be listed."""
    comparison = github(f'/repos/{repository}/compare/{old}...{new}')
    if comparison.get('status') not in ('ahead', 'identical'):
        return f'{old[:12]} is not an ancestor of {new[:12]}'
    files = comparison.get('files') or []
    if len(files) >= COMPARE_LIMIT:
        return f'more than {COMPARE_LIMIT - 1} files changed since {old[:12]}'
    paths = set()
    for entry in files:
        paths.add(entry['filename'])
        if entry.get('previous_filename'):
            paths.add(entry['previous_filename'])
    return paths


def main_carries(since):
    """Whether a push to main carries the last full run's result forward:
    only when everything changed since is documentation, packaged pages
    included, that the candidates need not run for (jrp, 21 November 2026:
    keep the full post-merge run whenever code or tests change). The version
    bot's VERSION commit after each merge starts no run, so it is always in
    the comparison; it is left out, and the tag's own runs test it before a
    release."""
    paths = [path for path in since if path != 'VERSION']
    return all(carried_kind(path) in ('documentation', 'packaged') for path in paths) and not candidates(
        paths, carried_kind
    )


def matrix(text, *, full):
    """A matrix's entries, listed as JSON (lines starting with # are
    comments), keeping only the Linux ones unless every platform runs."""
    entries = json.loads('\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('#')))
    return entries if full else [entry for entry in entries if entry['os'].startswith('ubuntu')]


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
    parser.add_argument('--github-output', help='append build=…, package=…, full=… and proving=… here')
    parser.add_argument(
        '--matrix',
        action='append',
        default=[],
        metavar='NAME=VARIABLE',
        help='also write NAME=, the JSON matrix in environment variable VARIABLE, '
        'keeping only its Linux entries for a draft pull request (CI-54)',
    )
    parser.add_argument(
        '--fuzz-sources',
        metavar='FILE',
        help="check that FUZZED names every repository file in this list of the fuzz targets' sources "
        '(one per line, - for standard input), and do nothing else',
    )
    parser.add_argument('paths', nargs='*', help='changed paths, instead of asking git')
    args = parser.parse_args(argv)
    if args.fuzz_sources:
        return check_fuzz_sources(args.fuzz_sources)

    payload = event_payload()
    full = full_run(args.event, payload)
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
        if workflows and 'fuzz' not in workflows:
            lines.append(
                'The fuzz targets do not run: nothing they are built from changed (CI-34). '
                'To run them for this branch, start "Fuzz smoke tests" by hand on it.'
            )

    # CI-53: carry a passed result forward when nothing this workflow needs
    # changed since its last full run.
    carried = False
    if args.workflow and not args.paths and runs(answer) and args.event in ('pull_request', 'push'):
        since, base = carried_from(args.workflow, args.event, payload)
        if since is None:
            lines.append(f'Nothing carried forward: {base}.')
        else:
            if args.event == 'push':
                carried = main_carries(since)
            else:
                carried = not since or not runs(decide(since, args.workflow, carried_kind))
            lines.append(
                f'Since run {base["id"]} passed in full on {base["head_sha"][:12]} ({base.get("html_url", "")}), '
                f'{len(since)} file(s) changed, on the branch or on main:'
            )
            lines.extend(f'  {carried_kind(path):13} {path}' for path in since)
            if carried:
                answer = {'build': False, 'package': False}
                lines.append(
                    f'None of them needs this workflow ({args.workflow}): its jobs are skipped and '
                    f'the result of run {base["id"]} stands (CI-53).'
                )
            else:
                lines.append(f'This workflow ({args.workflow}) needs them, so it runs.')
    if not full:
        lines.append(
            'A draft pull request runs Lint and the Linux jobs only (CI-54). Marking it ready runs every '
            'platform; to run them sooner, start the workflows by hand on the branch.'
        )
    if args.workflow:
        lines.append(f'This workflow ({args.workflow}) ' + ('runs.' if runs(answer) else 'is skipped.'))
    outputs = dict(answer, full=full, proving=runs(answer) and full and not carried)
    for option in args.matrix:
        name, _, variable = option.partition('=')
        outputs[name] = json.dumps(matrix(os.environ[variable], full=full), separators=(',', ':'))
    print('\n'.join(lines))
    if args.event == 'pull_request' and outputs['proving']:
        merged_into = pull_request_base()
        if merged_into:
            # The record a later run checks before it carries this result.
            print(f'::notice title={BASE_TITLE}::{merged_into}')
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary and args.workflow:
        with Path(summary).open('a', encoding='utf-8') as stream:
            stream.write('### CI scope\n\n```text\n' + '\n'.join(lines) + '\n```\n')
    if args.github_output:
        with Path(args.github_output).open('a', encoding='utf-8') as stream:
            stream.writelines(
                f'{key}={value if isinstance(value, str) else str(value).lower()}\n' for key, value in outputs.items()
            )
    return 0


if __name__ == '__main__':
    sys.exit(main())
