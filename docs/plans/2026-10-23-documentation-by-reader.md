<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Documentation by reader (DOC-4)

[Documentation index](../README.md) · [Documentation guide](../develop/documentation.md) ·
[TODO](../../TODO.md)

Plan dated 23 October 2026, from a review of every maintained page on `main`
at `7f170a8` (0.4.24). jrp asked for documentation that is "more tractable and
less of a magical mystery tour": separate broad guidance from reference
material and from instructions for different audiences, so readers get to what
they need quickly, clearly rather than tersely. jrp chose the scheme below on
23 October 2026 and asked that the developer pages double as agent
instructions, so that separate skills are not needed.

**In short:** maintained pages move into three reader folders, `docs/use/`,
`docs/develop/` and `docs/project/`; each page is one kind (tutorial, how-to,
reference or background) and follows the
[writing rules](../develop/documentation.md#writing-rules). Four pull requests,
one per step. Step 1 (`docs/develop/`) is done in the pull request that adds
this plan. Dated records do not move.

## What was wrong

- Pages were organised by component (core, VST3, AU, LV2), so each format guide
  ran user steps, build recipes with SDK pins, internals and test evidence in
  one page.
- Two overlapping front doors (`README.md` and `docs/README.md`).
- Build commands in 11 pages, with different folder names and details.
- Three plug-in routes (`vst3.md`, `docs/plugins/`, `presets.md`) that
  disagreed about status.
- Status answered in four places (`REVIEW.md`, release readiness,
  modernization, TODO).
- The shared adapter contract lived only in a plan, cited 25 times as the
  authority.
- TODO IDs, pull request numbers and commit hashes in user guides.
- Records beside current guides under five different folder names.

## Target tree

```text
README.md, CONTRIBUTING.md, AGENTS.md, CLAUDE.md, TODO.md, CHANGELOG.md
docs/README.md               index: three doors, every page with its kind, moved pages
docs/use/                    people applying filters; shipped whole in preview packages
  README.md                  start page: file, plug-in host or Windows system-wide
  concepts.md                background
  first-filter.md            tutorial: the bundled examples with the CLI
  first-sound-in-a-host.md   tutorial
  install.md                 how-to: package and every plug-in format on every OS
  presets.md                 how-to
  equalizer-apo.md           how-to
  other-programs.md          how-to: Roon, JRiver, Audiolense, old Convolver
  troubleshooting.md         how-to: symptom, cause, fix
  command-line.md            reference
  configuration.md           reference
  plugins.md                 reference: behaviour, with a VST3 / AU / LV2 table
  limits.md                  reference: what Convolver does not do
docs/develop/                building, embedding, packaging, changing (people and agents)
  build.md, library.md, architecture.md, plugin-contract.md, plugin-identities.md,
  fft-backends.md, testing.md, host-testing.md, releasing.md, workflow.md,
  documentation.md, versioning.md
docs/project/                status, direction and decisions
  status.md, decisions.md, history.md, plans/
docs/archive/                records; future records go here
docs/review-2026-10/, docs/benchmarks/, docs/historical/   stay put
```

`docs/historical/`, `docs/review-2026-10/` and `docs/benchmarks/` stay where
they are: frozen JSON records and `tests/compatibility/cases.json` cite their
paths, and records are never edited.

## Where each remaining page goes

| Today | Goes to |
| --- | --- |
| `portable-core.md` | Rendering, gain, dither, speaker labels, output safety, streams, FFmpeg, inspection and FFTW tuning to `use/command-line.md`; Engine API and resource limits to `develop/library.md`; the dated dependency record to `archive/` |
| `vst3.md`, `au.md`, `lv2.md` | Installing to `use/install.md`; loading, buses, latency, bypass, ports and the AU licence note to `use/plugins.md`; test evidence to `develop/testing.md` |
| `presets.md` | Making, converting and loading presets, and messages, to `use/presets.md`; file contents to `develop/plugin-contract.md` |
| `concepts.md`, `configuration.md`, `equalizer-apo.md` | `use/`; Equalizer APO's "how it is tested" to `develop/testing.md` |
| `configuration-compatibility.md` | `use/other-programs.md`; corpus internals to `develop/testing.md` |
| `plugins/index.md`, `install.md`, `quickstart.md`, `troubleshooting.md` | `use/README.md`, `use/install.md`, `use/first-sound-in-a-host.md`, `use/troubleshooting.md` |
| `plugins/build.md`, `plugins/hosts.md` | `develop/build.md` (pinned recipe and pins), `develop/host-testing.md` |
| `release-readiness.md`, `REVIEW.md`, `modernization.md` | `project/status.md`; dependency choices to `develop/architecture.md` |
| `releases/developer-preview.md` | Procedure to `develop/releasing.md`; the dated record to `archive/releases/` |
| `history.md`, `plans/` | `project/` |
| `convolverPipe.md` | `archive/` (historical tool) |

## Steps

1. **Done (this plan's pull request):** `docs/develop/`, `build.md`,
   `plugin-contract.md`, the writing rules and rules for pages agents follow,
   the full review procedure in `workflow.md` with thin skills.
2. **`docs/use/`:** split the CLI and format guides, merge `docs/plugins/`, and
   ship `docs/use/` in preview packages (`cmake/ConvolverPluginPackaging.cmake`,
   `scripts/release/package_docs.py`, `tests/release/docs_tests.py`,
   `tests/release/package_smoke.py`, `scripts/release/verify_artifact.py`).
   Remove TODO IDs and commit references from the user pages as they move.
3. **`docs/project/`:** one status page, a decisions log started from the
   decisions recorded in TODO, plans and reviews, then history and plans.
4. **Front doors and checks:** a README of about 80 lines, a shorter
   `AGENTS.md` that links instead of copying, TODO with open work only, and a
   repository check that every maintained page sits in a reader folder. Make
   `check_repository.py` fail when a file whose paths it checks is missing,
   instead of skipping it.

## Decisions

jrp accepted all five proposals on 23 October 2026 (owner's decision):

1. **One user page for all plug-in formats:** `use/plugins.md`, with a
   per-format table, since most behaviour is shared.
2. **TODO holds open work only:** finished items are dropped, since each is in
   `CHANGELOG.md` and its pull request.
3. **`REVIEW.md` is retired** into `project/status.md`.
4. **All of `docs/use/` ships** in preview packages.
5. **A decisions log** is kept in `project/decisions.md`.
