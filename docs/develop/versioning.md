<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Source versions

[Documentation index](../README.md) · [Changelog](../../CHANGELOG.md)

**In short:** `VERSION` holds the one version number. Each source change merged
to `main` gets the next patch number and a `vX.Y.Z` tag automatically; for a
deliberate minor or major step, label the pull request `version:minor` or
`version:major` rather than editing `VERSION`. Changes confined to
`docs/` and top-level Markdown files do not bump the version. A tag identifies a
revision; it does not certify a tested release.

The portable library, CLI and plug-ins share the number in [`VERSION`](../../VERSION):
`MAJOR.MINOR.PATCH`. CMake package metadata and numeric bundle versions read that
file. Historical Windows resource versions and the old `convolver*` tags retain
their original meaning.

## Automatic patch versions

Every push to `main` runs **Version source changes**. It checks for source changes
since the latest reachable portable `vX.Y.Z` tag, increments PATCH, commits only
`VERSION`, and pushes that commit and an annotated tag together. Starting from
`0.2.0`, the next automatic version is `0.2.1`.

The source scope is defined once in `scripts/version_source.py`: application and
library sources, historical sources, tests, CMake files, scripts, workflows and
`packaging/` (installer sources, trademarks, the release policy and the release
notes, so that a preview's release-notes pull request makes the tag the preview
is built from). Changes confined to `docs/`, top-level Markdown guides or a full
release's evidence (`packaging/evidence/`, read only from `main`) do not
increment the version. Files inside the source/build/test directories count even if they are
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

For a deliberate feature/API milestone, add the label `version:minor` to the
pull request; for a major compatibility milestone, `version:major`. Leave
`VERSION` itself alone: every pull request that edited it conflicted with the
version bot's next commit and with every other such pull request
([decision](../project/decisions.md#documentation)). If you cannot add a
label yourself, ask jrp to add it before he merges, and say so in the pull
request. Only these two exact names count: any other label, a misspelt one
included, leaves the automatic patch. When the merge reaches `main`, the workflow reads the
labels of the merged pull requests behind every source change since the last
tag (`scripts/version_source.py --labels-from`, through the GitHub CLI with
the workflow's token) and takes the largest step any of them asks for: from
`0.17.8`, a `version:minor` label gives `0.18.0` and `version:major` gives
`1.0.0`. A label on a pull request that changes no source counts for nothing,
since such a merge gets no version. If the labels cannot be read, the run
fails without committing or tagging; run it again once GitHub answers. Add
the label before merging: the run that versions the merge reads it once, and
a tag is never moved.

An explicit higher number in `VERSION` is still honoured, for recovery by
hand, and a label never lowers it. Until 1.0, API incompatibilities belong in
minor versions, and a CMake consumer that asks for a version needs the same
minor version; from 1.0 they belong in major versions, and the consumer needs
the same major version (`ConvolverConfigVersion.cmake`, REL-18). The API is
the installed core and I/O headers ([package guide](library.md)), not the
plug-ins' host layer.

Use the changelog, through each pull request's
[change fragment](workflow.md#change-fragments), for noteworthy human-readable
changes. It need not acquire an entry for every automatic patch. Do not edit archived release records or change
plug-in IDs or serialized-state versions to match the package number.

## LV2 versions

LV2 plug-ins carry `lv2:minorVersion` and `lv2:microVersion` instead of a
version string. LV2 has no major version, reads an odd minor or micro as a
development version and minor 0 as experimental, and lilv loads the highest
pair it finds. `plugins/lv2/generate_ttl.py` therefore doubles `VERSION`:
minor is 2 × (100 × MAJOR + MINOR) and micro is 2 × PATCH, so 0.13.6 is
26.12 and 1.0.0 is 200.0, above every 0.x. Every release is even and later
versions are higher (`lv2.version`). A MINOR of 100 or more stops the build,
since the rule would then need changing.

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
python3 scripts/version_source.py --push --labels-from jrp2014/convolver
```

This reads the version labels through the GitHub CLI, which must be signed
in, as the workflow does. `--push` refuses to run without `--labels-from`,
since a missed `version:minor` would publish a patch that no tag move can
undo; `--ignore-labels` replaces it only when you have checked that no pull
request merged since the last tag has a version label. Omit `--push` to create only
the local version commit and tag. This is a mutation, not a dry run. If publication failed after local creation, use a fresh clone for
recovery; do not force-push or move a published tag.
