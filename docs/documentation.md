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
| FFT selection and performance evidence | [FFT guide](fft-backends.md) | Adapter contract; dated measurement artifacts |
| Verification | [Testing](testing.md) | CMake registrations, workflow matrix, actual run results |
| Open work | [TODO](../TODO.md) | Current source and linked issues/plans |
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
reviews and plans as records rather than updating their results in place. The
`superpowers/` folder name is a workflow convention: its plans are design records,
not special authority over current code or user instructions. Put source comments
near non-obvious invariants; put contributor navigation here, rather than adding
large instruction blocks to implementation files.

External references should point to primary specifications, project repositories
or upstream documentation. Record the exact release/hash when adopting code.
An upstream link alone is not a licence audit, performance result or proof that
our vendored copy is current.

## Automated checks

The pre-commit configuration used by lint CI runs
[`check_doc_links.py`](../scripts/check_doc_links.py) and its
[regression tests](../tests/documentation_links_tests.py). The checker scans
top-level `.md` files and Markdown under `docs/`, excluding `archive/`,
`historical/`, `review-2026-10/` and `superpowers/` as source pages. Links to those
records are still checked. This keeps preserved evidence separate from maintained
instructions without accepting broken navigation into the evidence.

The check covers inline and reference-definition destinations, ATX headings
(`# Heading`, including duplicate headings) and HTML `id`/`name` anchors. Code
fences and inline code are ignored. It is a deliberately small, offline check,
not a complete Markdown renderer: it does not validate external URLs, detect
undefined reference labels, or verify commands and source claims. Review those
separately; extend the checker with a regression test if maintained guides need
additional Markdown syntax. DOC-2 in [TODO](../TODO.md) tracks remaining automation.
