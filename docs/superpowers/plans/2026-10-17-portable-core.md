# Portable core and CLI implementation plan

**Goal:** Implement the portable core/CLI milestone approved by the owner, using
real FFTW and libsndfile, with no push.

**Architecture:** Independently compiled C++20 library, prepared uniform
partitioned FFT engine, libsndfile I/O and legacy configuration importer, and a
streaming CLI. Historical Windows sources remain a reference and regression
baseline. This executes the native-core milestone in [the modernization design](../../modernization.md).

**Execution:** Parallel independent engine and I/O modules behind the shared
headers; coordinator implements CMake, CLI and integration tests, then reviews
and validates the combined result. The user's go-ahead authorizes implementation.

## Contracts and scope

- `include/convolver/engine.hpp`: in-memory Configuration/FilterPath/Route and
  Engine; interleaved float input/output, arbitrary caller frame counts, empty
  input for drain, fixed B-frame latency, explicit tail excluding latency.
- `include/convolver/io.hpp`: libsndfile reader/writer and configuration loader.
  Preserve zero-as-unity, signed routes, truncating milliseconds to frames,
  routing/mixing, channel selection and mismatch rejection. Relative IR paths
  resolve from their config file. Lists select matching sample rate/input count.
- CLI: `convolver --filter FILE --input FILE --output FILE`, optional
  `--block-size N`, `--gain-db DB`, `--keep-latency`, `--output-format FORMAT`.
  `-` means standard stream; RAW float32 little endian uses explicit input rate
  and channels. Default WAV float output trims algorithmic latency, retains FIR
  and routing tail. Empty input produces no audio frames.
- All allocation and FFT planning outside process. CMake C++20, dependency
  discovery without automatic downloads, selectable sanitizer tests.
- No new FFT backends, resampling, device I/O or plug-in port in this milestone.

## Review focus

Partial last blocks and exact EOF; repeated reset and zero-frame calls; negative
zero/channel weights and relative paths; bad rates/sizes/nonfinite data; RAW
non-seekable descriptors and failure propagation must have behavioural tests.

## Tasks

1. Commit existing documentation/test improvements after CTest passes (completed
   as `eeef1a4`). Preserve unrelated untracked archive/review files.
2. Write independent time-domain oracle tests for Engine before its implementation;
   implement FFTW uniform partitioning, mixing/delays and reset, validate real FFTW
   scaling, arbitrary chunk boundaries, tails and invalid arguments.
3. Write real libsndfile and parser tests before implementing I/O/configuration;
   test PCM/float files, actual descriptor streams, channel selection, rate
   mismatch, signed route syntax, lists/cycles and malformed files.
4. Build CMake targets `convolver_core`, `convolver_io`, `convolver_cli`, integrate
   tests, and write CLI subprocess tests for exact lengths, independent expected
   audio, both latency modes, failures and RAW pipelines before completing CLI.
5. Run complete native plus historical CTest suites under sanitizers and Release,
   inspect native linkage, smoke-test installed CLI, document supported semantics
   and remaining platform verification limits. Commit verified implementation;
   leave local main ahead of GitHub without pushing.

## Completion record — 18 October 2026

Implemented all five tasks. The complete 12-test CTest suite passed in both
AppleClang Debug with ASan/UBSan and Release, linked against real FFTW 3.3.11 and
libsndfile 1.2.2. CLI integration tests also passed against the installed Release
executable. Independent review reproductions verified protection of referenced
IR files and redirected stdin, and rejection of truncated WAV input. Follow-up
review found no remaining actionable issues. Linux and Windows CI are configured
but have not run for these unpushed changes; native validation here is macOS only.
