# FFT backends implementation plan

**Goal:** Add Apple Accelerate while retaining FFTW, prove equivalent convolution
results, and measure both on this Mac. Execute the user's requested work locally.

**Design:** Select one backend per build with `CONVOLVER_FFT_BACKEND` (FFTW by
default, ACCELERATE on Apple only). An internal prepared RealFft owns its buffers
and exposes canonical interleaved complex bins 0..N/2, an unscaled forward DFT,
and an unnormalized inverse. Engine normalizes IR spectra once by 1/N. Accelerate
uses radix-2 real FFTs and pads N to the next power of two, keeping audio block B,
latency, routes, delays and tail unchanged. FFTW retains N=2B. No processing-time
allocation, planning, backend switching or dependency on FFTW in Accelerate builds.

**Files:** portable/fft.hpp and fft_{fftw,accelerate}.cpp; engine.cpp consumes the
interface; engine.hpp/main.cpp expose backend identification; CMake selects the
dependency; native FFT tests, paired CLI equivalence test, and benchmark tools.

**Review focus:** DC/Nyquist packing, factor-of-two real FFT scaling, B=1 and odd B,
resource accounting with padded N, and repeated processing/reset without allocation.

- [x] Add transform-oracle tests and retain existing convolution/CLI tests.
- [x] Extract FFTW implementation and implement Accelerate; run complete sanitizer
      and Release suites for each and paired CLI output comparisons.
- [x] Add a reproducible processing benchmark, alternate backend run order, report
      median throughput and setup separately; record hardware/compiler/dependencies.
- [x] Update CI for both Apple backends and paired output verification; document
      choices, measurements and limitations. Review final diff and leave changes
      local unless pushing is requested.

Completed locally with 13/13 suites for both backends in Debug/sanitizer and
Release, paired CLI comparison, seven-round NEON FFTW/Accelerate benchmark,
and independent static review with no actionable findings. See ../../fft-backends.md.

Follow-up: explicit estimate/measure/patient planning, offline `--tune-fft`, and
native FFTW wisdom import/export are implemented. Fresh-process CLI tests verify
reuse and errors before output creation; direct FFT/matrix oracles cover measured
plans. Both backends now pass 14/14 suites in Debug/sanitizer and Release. Review
identified the exported wisdom string's ordinary-malloc ownership; it now uses
`std::free` as required by FFTW, separately from aligned FFT buffers. Remote
Windows/Linux/macOS results are tracked in the GitHub Actions workflow runs.

The user's next priority is overall structure, maintainability and performance;
see [the current delivery plan](../../project-structure.md). Further FFT library
experiments are deferred in favor of packaging, compatibility and transform reuse.
