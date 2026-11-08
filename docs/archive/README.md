<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Archive

[Documentation index](../README.md)

Dated records of what was true, decided or measured at the time. They are kept
as written and are not instructions: for how Convolver behaves now, use the
[current documentation](../README.md); for what is ready, the
[status page](../project/status.md).

| Folder | What it holds |
| --- | --- |
| [`plans/`](plans/README.md) | Finished and superseded design and implementation plans, with what each delivered |
| [`reviews/`](reviews/README.md) | Dated reviews, host-test records and test evidence |
| [`releases/`](releases/README.md) | Release candidate records and their evidence |
| [`todo/`](todo/README.md) | The full entries and one-line records of finished TODO items |
| [`documentation-2026-10-20/`](documentation-2026-10-20/README.md) | Byte-for-byte snapshots of the documentation before its 20 October 2026 review |

Two older record folders stay where they are, because frozen records and tests
cite their paths: [`../review-2026-10/`](../review-2026-10/) (the October 2026
review and recovery evidence) and [`../benchmarks/`](../benchmarks/) (the raw
measurements behind the [FFT and engine measurements](../develop/fft-measurements.md)).

## Moved pages

These pages moved on 23 October 2026, when the documentation was sorted by
reader. Dated records still link to the old paths.

| Old path | New path |
| --- | --- |
| `docs/project-structure.md` | [`docs/develop/architecture.md`](../develop/architecture.md) |
| `docs/cmake-package.md` | [`docs/develop/library.md`](../develop/library.md) |
| `docs/fft-backends.md` | [`docs/develop/fft-backends.md`](../develop/fft-backends.md) |
| `docs/plugin-identities.md` | [`docs/develop/plugin-identities.md`](../develop/plugin-identities.md) |
| `docs/testing.md` | [`docs/develop/testing.md`](../develop/testing.md) |
| `docs/workflow.md` | [`docs/develop/workflow.md`](../develop/workflow.md) |
| `docs/documentation.md` | [`docs/develop/documentation.md`](../develop/documentation.md) |
| `docs/versioning.md` | [`docs/develop/versioning.md`](../develop/versioning.md) |
| Shared adapter contract in the plug-in formats plan | [`docs/develop/plugin-contract.md`](../develop/plugin-contract.md) |
| `docs/concepts.md`, `configuration.md`, `equalizer-apo.md`, `presets.md` | The same names in [`docs/use/`](../use/README.md) |
| `docs/portable-core.md` | [`docs/use/command-line.md`](../use/command-line.md); the engine API and limits are in the [library guide](../develop/library.md#using-the-engine) |
| `docs/configuration-compatibility.md` | [`docs/use/other-programs.md`](../use/other-programs.md) |
| `docs/vst3.md`, `docs/au.md`, `docs/lv2.md` | [`docs/use/plugins.md`](../use/plugins.md) and [`docs/use/install.md`](../use/install.md); their tests are in [testing](../develop/testing.md#plug-in-format-tests) |
| `docs/plugins/index.md`, `install.md`, `quickstart.md`, `troubleshooting.md` | [`docs/use/README.md`](../use/README.md), [`install.md`](../use/install.md), [`first-sound-in-a-host.md`](../use/first-sound-in-a-host.md), [`troubleshooting.md`](../use/troubleshooting.md) |
| `docs/plugins/build.md`, `docs/plugins/hosts.md` | [`docs/develop/build.md`](../develop/build.md#preview-recipe), [`docs/develop/host-testing.md`](../develop/host-testing.md) |
| `REVIEW.md`, `docs/release-readiness.md`, `docs/modernization.md` | [`docs/project/status.md`](../project/status.md); dependency choices in the [implementation map](../develop/architecture.md#dependency-choices); the old review is [archived](../archive/reviews/2026-10-20-repository-review.md) |
| `docs/releases/developer-preview.md` | [`docs/develop/releasing.md`](../develop/releasing.md); the record is [archived](../archive/releases/README.md) |
| `docs/history.md`, `docs/convolverPipe.md`, `docs/plans/` | The same names in `docs/project/` |
