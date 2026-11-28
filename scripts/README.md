<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Scripts

The scripts that build, test, check, version and release Convolver, for
contributors and the CI workflows. Scripts that look after a copy of the
repository or your own computer are in [`maintenance/`](maintenance/README.md)
instead; where a new script goes is in
[where a script goes](../docs/develop/architecture.md#where-a-script-goes).
Each script listed below explains its options with `--help` or in its
opening comment, and has a test under [`tests/`](../tests/).

## Checks that pre-commit runs

| Script | What it does | Test |
| --- | --- | --- |
| [`check_format.py`](check_format.py) | Checks the portable C++ against the latest stable clang-format, or fixes it with `--fix` | `tests/format_tools_tests.py` |
| [`check_doc_links.py`](check_doc_links.py) | Checks the local links and anchors in the maintained Markdown | `tests/documentation_links_tests.py` |
| [`check_repository.py`](check_repository.py) | Checks the repository-wide rules in [automated checks](../docs/develop/workflow.md#automated-checks) | `tests/repository_checks_tests.py` |
| [`trademarks.py`](trademarks.py) | Checks, or writes with `--fix`, the VST trademark wording | `tests/trademarks_tests.py` |
| [`fold_changes.py`](fold_changes.py) | Checks the change fragments (`--check`), or folds them into the shared records ([change fragments](../docs/develop/workflow.md#change-fragments)) | `tests/fold_changes_tests.py` |

## CI

| Script | What it does | Run by | Test |
| --- | --- | --- | --- |
| [`ci_scope.py`](ci_scope.py) | Decides which jobs a pull request needs from the files it changes ([CI triggers](../docs/develop/testing.md#ci-triggers)) | The `scope` job of each workflow that builds on pull requests | `tests/ci_scope_tests.py` |
| [`ci_evidence.py`](ci_evidence.py) | Collects the logs and reports a failed test run leaves, for upload | The VST3, AU, LV2 and regression workflows | `tests/ci_evidence_tests.py` |
| [`fuzz_campaigns.sh`](fuzz_campaigns.sh) | Runs the fuzz campaigns | The fuzz workflow | `tests/fuzz/scheduler_tests.py` |
| [`version_source.py`](version_source.py) | Commits and tags the next version after a merge to `main` ([versioning](../docs/develop/versioning.md)) | The version workflow | `tests/versioning_tests.py` |

`version_source.py` creates a commit and a tag even without `--push`, so do
not run it to try it out.

## Host checks

| Script | What it does | Test |
| --- | --- | --- |
| [`reaper_check.py`](reaper_check.py) | Runs the REAPER checks on a Mac against an unzipped preview package ([host testing](../docs/develop/host-testing.md)) | `tests/reaper_check_tests.py` |

## Releases

[`release/`](release/) holds the steps that resolve and fetch dependencies,
build candidates, make installers and prepare a draft release. They are
described where they are used: [building from source](../docs/develop/build.md)
and [releasing](../docs/develop/releasing.md). Their tests are in
[`tests/release/`](../tests/release/).
