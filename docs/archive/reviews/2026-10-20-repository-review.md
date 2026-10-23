<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
> **Archived review.** This was `REVIEW.md` at the repository root until 23 October 2026, when it was retired into [status](../../project/status.md) (what is ready, and what the tests do not establish) and the [implementation map](../../develop/architecture.md) (which source implements each behaviour). The text below is unchanged; its links are relative to the repository root, where the page then lived.

# Convolver: current repository review

Documentation and source cross-check dated **20 October 2026**, baseline
`2fe503e33800b82f5c4f83c972ee0ecef2b5bc9d`. This is a status guide, not a claim of
an exhaustive new code audit. See [VERSION](VERSION) for the package version and
[documentation review findings](docs/archive/reviews/2026-10-20-documentation-review.md) for this update.

For what users can run now, the evidence for it and what blocks the next
release, see [release readiness](docs/release-readiness.md); this page maps
implemented behaviour to its source and checks.

New readers should start with the [README](README.md) or
[documentation index](docs/README.md). Contributors and agents should use the
[implementation map](docs/develop/architecture.md) and [repository rules](AGENTS.md).

## Correctness and compatibility

| Implemented behavior | Source / maintained explanation | Main checks |
| --- | --- | --- |
| Uniform partitioned overlap-add with shared forward transforms for identical ordered input mixes | [Engine](portable/engine.cpp), [processing flow](docs/develop/architecture.md#processing-data-flow) | Direct-double oracle, routing/delays, awkward blocks, chunk/reset and heap guards |
| FFTW, Apple Accelerate and built-in Ooura | [FFT contract](portable/fft.hpp), [backend guide](docs/develop/fft-backends.md) | Independent direct DFT and convolution tests; CLI backend comparisons |
| Configuration/list loading and inspection, including Windows relative paths | [Current grammar](docs/use/configuration.md), [compatibility](docs/use/other-programs.md) | Parser, inspection, limits, generated corpus and fuzz replay |
| Float/RAW I/O and PCM8/16/24/32 quantization, optional dither, shaping and valid bits | [I/O implementation](portable/io.cpp), [command-line reference](docs/use/command-line.md#integer-formats-dither-and-noise-shaping) | Real files, exact samples/header fields, statistics, stream faults |
| Fixed and automatic gain estimates | [Attenuation](portable/attenuation.cpp), [CLI behavior](docs/use/command-line.md#gain-and-attenuation) | Engine oracle cases for the mathematical peak bound |
| Persistent host lifecycle and VST3 with 1–8 input and 1–8 output channels | [Host API](include/convolver/host.hpp), [plug-in guide](docs/use/plugins.md) | Host/state tests and real SDK adapter, preset and bundle-validator tests |
| Audio Unit (AUv2) effect with 1–8 input and 1–8 output channels, built from source | [plug-in guide](docs/use/plugins.md) | In-process AU host tests (PLUG-2 comparisons, ClassInfo, bypass, silence flag, allocations), bundle checks and `auval -strict` on macOS CI |
| Independent CMake core/host/io components | [Package guide](docs/develop/library.md) | Installed/relocated and source-tree consumers |

Tests support the exercised behavior, not every possible input or platform.
The normal CLI trims processing latency and drains the full tail. The streaming
core preserves history and reports latency/tail separately. The VST3 adapter
reports 256 samples of latency. These contracts are different from merely
convolving each host callback independently.

No implicit resampling is provided. Configuration-format adoption by another
program does not establish identical selection, path or routing behavior.

## Verification evidence and limits

[Testing](docs/develop/testing.md) maps tests to production behavior. The
[native workflow](.github/workflows/regression.yml),
[VST3 workflow](.github/workflows/vst3.yml),
[fuzz workflow](.github/workflows/fuzz.yml) and
[lint workflow](.github/workflows/lint.yml) (formatting and the other
pre-commit checks) describe the configured gates.
Check [Actions](https://github.com/jrp2014/convolver/actions) for a particular
revision's results; neither a workflow file nor a source tag proves a pass.

The [18 October validation record](docs/review-2026-10/current-validation.md)
belongs to its recorded 0.2.0 baseline. It is retained as evidence, not relabelled
as a current run. Likewise, [performance measurements](docs/develop/fft-backends.md)
apply to the stated source/hardware/builds. There is no universal fastest FFT
backend established by those measurements.

Automated tests do not establish compatibility in a named commercial DAW,
hard real-time deadlines, signed/notarized distribution, or native builds of the
historical Windows applications. Open work, including release qualification and
further FFT investigation, is maintained in [TODO.md](TODO.md).

## Downstream use and preserved documentation

The [history guide](docs/history.md#downstream-use) separates evidence of derivative
code from use of the configuration format. Historical source, recovered websites,
old findings and import provenance belong there and in the linked archives.
They are not prerequisites for using the portable implementation.

The 4.4 website remains byte-preserved under `docs/historical/`. Current
instructions live in maintained Markdown. Do not repair an obsolete archived
instruction by editing the archived original.
