<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Documentation review: 20 October 2026

> **Archived record.** Moved on 21 October 2026 from
> `docs/documentation-review-2026-10-20.md`; only link locations changed.

[Documentation index](../../README.md) · [Current repository review](../../../REVIEW.md)

This review checks the maintained documentation against source revision
`2fe503e33800b82f5c4f83c972ee0ecef2b5bc9d` (package 0.4.0), then improves its
organization and wording. It is not an exhaustive new code audit or a production
release qualification. Documentation edits and the link-checking tool described
here are local changes on that baseline; DSP and plug-in source are unchanged.

## Findings and corrections

| Finding | Correction |
| --- | --- |
| Entry pages mixed historical Windows instructions, current features and future plans | Added a task-based index, concepts page and repository map; placed historical material behind clearly labelled links |
| Current status pages still named 0.2.0 and described inspection/corpus/fuzz work as future | Cross-checked implemented behavior against source and tests; consolidated remaining work in TODO |
| Some backend/build descriptions omitted Ooura | Documented all three implemented backends and the Ooura preset, with upstream references |
| Output guidance said PCM24/32 bypassed the portable quantizer | Corrected descriptions to cover PCM8/16/24/32 dither, shaping and valid-bit handling |
| The configuration examples and rules were scattered across old/current guides | Added one maintained configuration reference, including signed zero, fractional routes, delay truncation, paths and list selection |
| VST3 first-use instructions jumped from building to presets | Added host discovery steps and official location references; retained dependency/signing limits |
| Test totals and dated passes looked like current guarantees | Replaced copied totals with test-discovery commands; marked the old validation record's baseline explicitly |
| No-clip wording could imply protection against quantizer endpoint effects | Stated the ideal filter bound's assumptions; recorded endpoint/headroom tests as GAIN-1 |
| Implementation prose required development-history knowledge | Added source/symbol, dependency, invariant and change-to-test maps for contributors and agents |
| Duplicated status lists and unsupported “latest” claims could drift | Assigned each topic an owning page, used VERSION as authority, and added offline local-link checks to pre-commit |

Technical claims were checked against the engine, FFT adapters, configuration
parser/resolver, inspection paths, I/O quantizer, attenuation implementation,
host/state API, VST3 adapter, CMake targets/presets and test registrations.
An independent documentation reviewer checked the changed guides against source
and identified the VST3 discovery gap, a README formatting error and a compiler
compatibility caveat to retain; these were addressed.

## Information preservation and navigation

The [pre-review snapshot](../documentation-2026-10-20/README.md) retains
21 original guides and root documents, including unchanged companion documents.
Every snapshot has its original byte count and SHA-256 in
[the manifest](../documentation-2026-10-20/manifest.json), tied to the source
commit. The `.md.txt` files preserve exact wording without presenting obsolete
relative links as current navigation; the archive index links to the navigable
original source revision as well.

| Information previously spread across… | Maintained destination |
| --- | --- |
| README, old configuration website and portable guide | [Quick start](../../../README.md), [concepts](../../concepts.md), [configuration](../../configuration.md) |
| REVIEW, modernization and project-structure prose | [Current status](../../../REVIEW.md), [principles](../../modernization.md), [implementation map](../../project-structure.md) |
| Feature wish lists and release limitations | [TODO](../../../TODO.md), with stable IDs and a linked limitations inventory |
| FFT implementation details and numerical measurements | [FFT guide](../../fft-backends.md); historical measurements retain their configuration/context |
| CLI options, formats, resource bounds and API contracts | [Core reference](../../portable-core.md), linked from the shorter entry pages |
| Package, host and test detail | [CMake package](../../cmake-package.md), [VST3](../../vst3.md), [testing](../../testing.md) |
| Import history, downstream reuse and old validation | [History](../../history.md), dated review records and existing archives |

The original historical website under `docs/historical/` is unchanged. Previous
review evidence and dated plans remain available. Current numerical benchmark
tables, API caveats and compatibility details have not been replaced by broad
speed or compatibility claims. The snapshot preserves superseded recommendations
as evidence even where the current guides now direct readers elsewhere.

## Verification

Run locally on macOS/Apple Silicon against the baseline plus these uncommitted
documentation/tooling changes:

| Check | Result |
| --- | --- |
| Rebuild `build/presets/debug-sanitizers` | Passed, AppleClang Debug with ASan/UBSan, FFTW 3.3.11 NEON and libsndfile 1.2.2 |
| Full CTest in that build, parallel 4 | 28/28 passed, including native, corpus, fuzz replay, historical, versioning and external-consumer tests |
| Native engine's independent double oracle | Worst absolute error `5.26131e-07` |
| New stereo configuration example, extracted from the Markdown | Validation and inspection passed; direct-IR and configuration float renders preserved all three stereo input frames; worst identity-filter sample error `1.49011612e-08`; PCM24 render also succeeded |
| Snapshot manifest | All 21 snapshots matched original Git bytes, lengths and SHA-256 hashes |
| Historical documentation CTest | 99 file lengths/hashes, 18 pages, 589 local references, 9 fragments and 7 failure-detection checks passed; external URLs skipped |
| Local Markdown link checker | 23 maintained pages passed |
| Link checker regression suite | 5 tests passed; the code-example anchor regression was observed failing before its fix |
| Pre-commit | ShellCheck, Markdown, strict YAML, actionlint, local links and checker tests passed; new untracked guides were also supplied explicitly |
| Whitespace check | `git diff --check` passed |

Commands used include `cmake --build build/presets/debug-sanitizers --parallel 4`,
`ctest --test-dir build/presets/debug-sanitizers --output-on-failure --parallel 4`,
the two Python checks in [CONTRIBUTING](../../../CONTRIBUTING.md), and
`pre-commit run --all-files --show-diff-on-failure`. Example audio was generated
locally with an identity IR; no commercial IR was needed. The checker was also
independently reviewed and corrected so HTML anchors inside Markdown code
examples do not conceal broken links.

This run did not rebuild the VST3 adapter, rerun other FFT backends, execute
Windows/Linux commands or run remote CI. Those platform instructions were
cross-checked against source and configured workflows. Added primary-reference
URLs were checked selectively; there was no exhaustive external-link crawl.

## Remaining work

[TODO](../../../TODO.md) includes the explicit reminder to compare the vendored Ooura
implementation with upstream, alternative FFT candidates with primary references,
and limitations gathered from the guides. It separates deliberate contracts from
missing features. Shared runnable example assets and automated CLI/build-option
documentation checks remain DOC-1/DOC-2. This review does not claim to have tested
external configuration consumers, commercial DAWs, installers or signing.

Future edits should follow [documentation maintenance](../../documentation.md), update
the owning page beside the code change and keep results tied to an actual run.
