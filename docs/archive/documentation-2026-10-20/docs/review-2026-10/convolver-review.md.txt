# Convolver review: current repository

Updated 18 October 2026 for portable **0.2.0**, source baseline `5df874e`.
The authoritative [current review](../../REVIEW.md) now covers both the recovered
historical tree and the portable core, CLI, packages and VST3 adapter.

The former 4.3-only assessment is preserved as the
[archived branch review](../archive/reviews/4.3-branch-review.md). Its Python
model and [saved results](model-results.txt) remain historical evidence; they
are not the current real-library test gate.

## What changed since that review

- The old partition-stride and delay-history defects, plus the additional PCM,
  aliasing, gain and complex-arithmetic fixes, were reconciled into the historical
  source. See the [merge record](reconciliation.md) for the selected implementations
  and corrections to the original review's wording.
- The archive supplied corroborated 4.4 features and the SourceForge documentation.
  This was selective recovery, not a complete stable 4.5 release.
- The portable engine is a separate overlap-add implementation with real FFTW and
  Accelerate tests against independent references, split-spectrum MACs and explicit
  latency/tail behavior. It is no longer a proposed port tested only in Python.
- Package exports/presets, external-consumer checks, Windows configuration paths,
  formatting/static-analysis tooling, host lifecycle and the optional VST3 adapter
  are implemented. VST3 supports embedded presets and mono/stereo streaming.
- Portable PCM16 dither, commercial DAW validation, release packaging and broader
  configuration interoperability remain work to do. Historical Windows wrappers
  are not certified by portable or extracted tests.

See [fresh validation evidence](current-validation.md),
[test boundaries](../testing.md), [current architecture](../project-structure.md)
and [remaining modernization priorities](../modernization.md).
The [downstream evidence](downstream-evidence.json) distinguishes derivative
engine lineage from configuration-format adoption.
