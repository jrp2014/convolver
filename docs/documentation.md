<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Maintaining the documentation

[Documentation index](README.md) · [Implementation map](project-structure.md)

Write for a reader who has not seen the development conversations. Define audio
terms before relying on them, explain what an option does before its internal
mechanism, and use one working example before describing exceptions. Keep
headings descriptive, link text meaningful, and tables small enough to scan.
Longer guides open with a short **In short** summary, and the longest add a
contents line. When a change alters a summarized fact, update the summary too.

## One home for each topic

| Topic | Authoritative page | Check against |
| --- | --- | --- |
| Purpose and first command | [README](../README.md) | Build and run the example |
| Navigation and vocabulary | [Index](README.md), [concepts](concepts.md) | Current entry points; avoid unexplained abbreviations |
| Configuration grammar | [Configuration](configuration.md) | `config_parse.cpp`, `config.cpp`, corpus |
| CLI, formats and core API behavior | [Core/CLI reference](portable-core.md) | `main.cpp`, `inspection_cli.cpp`, public headers and tests |
| Component boundaries and change map | [Implementation map](project-structure.md) | Actual symbols, callers, targets and test names |
| Package consumption | [CMake package guide](cmake-package.md) | CMake exports, presets, external-consumer tests |
| Plug-in build and host behavior | [VST3 guide](vst3.md) | Adapter, SDK pin, host/state tests |
| Plug-in identities and channel layouts | [Plug-in identities](plugin-identities.md) | `plugins/identities.json`, `tests/identities_tests.py`, `vst3.identities` |
| Plug-in presets and the preset tool | [Plug-in presets](presets.md) | `portable/presets.*`, `portable/preset_main.cpp`, `native.presets`, `plugins.presets`, `vst3.preset_format` |
| System-wide filtering on Windows | [Equalizer APO export](equalizer-apo.md) | `portable/equalizer_apo*.cpp`, `export.equalizer_apo` and its model of Equalizer APO |
| Plug-in downloads and release gates | [Plug-in distribution](plugins/index.md), [release record](releases/developer-preview.md) | `packaging/release-policy.json`, `scripts/release/`, `tests/release/`, candidate workflow |
| Release status and evidence levels | [Release readiness](release-readiness.md) | TODO, open pull requests, GitHub releases, host records |
| FFT selection and performance evidence | [FFT guide](fft-backends.md) | Adapter contract; dated measurement artifacts |
| Verification | [Testing](testing.md) | CMake registrations, workflow matrix, actual run results |
| Open work | [TODO](../TODO.md) | Current source and linked issues/plans |
| Claiming, handing over and reviewing work | [Workflow](workflow.md) | Pull request template, repository checks, `.claude/skills/` |
| Past releases and provenance | [History](history.md) and dated records | Recorded evidence; never rewrite a past result as current |

Short summaries may link to these pages; avoid copying their option lists,
limits, test counts or TODOs into several places. `VERSION` is the numeric source
of truth. Version numbers in commands are examples or compatibility requirements,
not another release metadata source.

## Updating a behavior

For contributors and agents:

1. Identify the source symbol, its callers and tests using the implementation map.
2. Update the owning guide in the same change. State the observable behavior,
   defaults, failure behavior and any compatibility effect.
3. Update navigation only when the reader's route through the docs changes.
   Keep existing heading anchors where possible; update inbound links if renamed.
4. Run documented examples where applicable. Distinguish commands actually run
   from platform instructions checked against CI configuration.
5. Run lint and local-link checks from [CONTRIBUTING](../CONTRIBUTING.md). A link
   check establishes that a target exists, not that the prose agrees with code.
6. Put new work in TODO with a stable ID and completion evidence. Close it when
   implemented; do not leave completed features described as future proposals.

## Current claims and dated evidence

A source map can state a review date and commit. A test result needs its actual
revision, configuration and outcome. Benchmark results additionally need machine,
compiler, dependency versions, planner mode and workload. Avoid words such as
"latest", "all platforms" and "fully tested" without a defined scope.

Keep the historical website under `docs/historical/` byte-identical. Mark dated
reviews and plans as records rather than updating their results in place. Plans
in [`plans/`](plans/) are design records for open work, not special authority
over current code or user instructions. (Until 21 October 2026 they lived in
`docs/superpowers/`, a name taken from an agent tool's convention.) When the code on
`main` implements all of a plan, move it to [`archive/plans/`](archive/plans/README.md)
with an archive notice and a row in that index, and fix links to it. Put source
comments near non-obvious invariants; put contributor navigation here, rather
than adding large instruction blocks to implementation files.

External references should point to primary specifications, project repositories
or upstream documentation. Record the exact release/hash when adopting code.
An upstream link alone is not a licence audit, performance result or proof that
our vendored copy is current.

## Automated checks

The pre-commit configuration used by lint CI (which also checks C++ formatting) runs
[`check_doc_links.py`](../scripts/check_doc_links.py) and its
[regression tests](../tests/documentation_links_tests.py). The checker scans
top-level `.md` files and Markdown under `docs/`, excluding `archive/`,
`historical/` and `review-2026-10/` as source pages. Open plans in `plans/` are
checked like maintained guides. Links to the excluded
records are still checked. This keeps preserved evidence separate from maintained
instructions without accepting broken navigation into the evidence.

The check covers inline and reference-definition destinations, ATX headings
(`# Heading`, including duplicate headings) and HTML `id`/`name` anchors. Code
fences and inline code are ignored. It is a deliberately small, offline check,
not a complete Markdown renderer: it does not validate external URLs, detect
undefined reference labels, or verify commands and source claims. Review those
separately; extend the checker with a regression test if maintained guides need
additional Markdown syntax. DOC-2 in [TODO](../TODO.md) tracks remaining automation.
