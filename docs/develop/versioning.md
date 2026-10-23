<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Source versions

[Documentation index](../README.md) · [Changelog](../../CHANGELOG.md)

**In short:** `VERSION` holds the one version number. Each source change merged
to `main` gets the next patch number and a `vX.Y.Z` tag automatically; edit
`VERSION` yourself only for a deliberate minor or major step. Changes confined to
`docs/` and top-level Markdown files do not bump the version. A tag identifies a
revision; it does not certify a tested release.

The portable library, CLI and VST3 plug-in share the number in [`VERSION`](../../VERSION):
`MAJOR.MINOR.PATCH`. CMake package metadata and numeric bundle versions read that
file. Historical Windows resource versions and the old `convolver*` tags retain
their original meaning.

## Automatic patch versions

Every push to `main` runs **Version source changes**. It checks for source changes
since the latest reachable portable `vX.Y.Z` tag, increments PATCH, commits only
`VERSION`, and pushes that commit and an annotated tag together. Starting from
`0.2.0`, the next automatic version is `0.2.1`.

The source scope is defined once in `scripts/version_source.py`: application and
library sources, historical sources, tests, CMake files, scripts and workflows.
Changes confined to `docs/` or top-level Markdown guides do not increment the
version. Files inside the source/build/test directories count even if they are
documentation or fixtures. A revert still counts as a source change. The tag
records the source commit it versions.

Runs are serialized and use the latest `main`. Normally each merged source change
gets one patch version; several pushes arriving together can be collected in one
version. All source changes since the preceding tag are included. Re-running an
already completed run does not increment again. Existing tags are never moved.

Pull requests are built and tested without creating versions. Source tags identify
revisions; they do not assert that CI passed or publish a binary release. The bot's
push uses [`GITHUB_TOKEN`](https://docs.github.com/en/actions/concepts/security/github_token),
which does not trigger another push workflow, so it cannot start a versioning loop.
Existing CI runs on the incoming source commit. Production
binary release validation remains a separate step.

## Minor and major versions

For a deliberate feature/API milestone, change `VERSION` to a higher minor version
with PATCH reset, for example `0.3.0`, in the same change. Use a higher major version
for a major compatibility milestone. The workflow honours that explicit number
instead of incrementing it. Until 1.0, API incompatibilities belong in minor
versions; CMake consumers continue to require a matching minor version.

Use `CHANGELOG.md` for noteworthy human-readable changes. It need not acquire an
entry for every automatic patch. Do not edit archived release records or change
plug-in IDs or serialized-state versions to match the package number.

## Identifying a build

`convolver --version` and the VST3 factory report, for example,
`0.2.1+g0123456789ab`. A build with uncommitted files adds `.dirty`. The Git suffix
is refreshed when building, including after a commit or local source edit.
Numeric CMake and platform bundle versions remain `0.2.1`.

Source archives build without Git and report the number from `VERSION`. An archive
extracted inside another repository does not inherit that repository's identity.
Ignored local build products do not mark the checkout dirty.

After the bot has versioned a push, use `git pull --ff-only` before further work
and `git fetch --tags` when looking up source versions.

## Recovery and local checks

The workflow needs permission to write repository contents. Branch/tag rules must
permit its version commit and new tag; no protection is bypassed. If publication
is rejected or `main` moves during the run, neither ref is pushed partially. Run
**Version source changes** again from Actions after resolving the cause; it starts
with a fresh checkout and includes outstanding source changes.

To exercise the behavior without touching this repository's tags:

```sh
python3 tests/versioning_tests.py
```

For manual recovery in a clean, fully fetched checkout of `main`:

```sh
python3 scripts/version_source.py --push
```

Omit `--push` to create only the local version commit and tag. This is a mutation,
not a dry run. If publication failed after local creation, use a fresh clone for
recovery; do not force-push or move a published tag.
