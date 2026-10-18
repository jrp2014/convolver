# Host lifecycle and VST3 implementation plan

**Goal:** compatible VST3 integration around the existing portable core.
**Architecture:** portable prepared host processor and state codec; thin SDK adapter
and preset writer; real-library numerical, lifecycle and SDK compatibility tests.
**Spec:** ../specs/2026-10-18-vst3-host-design.md
**Execution:** inline, authorized by user; one independent final review. Existing
checkout branch isolates tracked changes; leave unrelated recovery files alone.

## Constraints and review focus

- Fixed 256-frame plugin latency; host callback sizes need not divide 256.
- No allocation, destruction, locking or exceptions in processing.
- Safe pending/current/retired ownership during concurrent state replacement.
- Parameter-only calls and silent inputs must preserve state and convolution tails.
- State corruption must not replace a working configuration or exhaust memory.
- Portable defaults; CMake 3.20 core, 3.25 optional pinned SDK 3.8.1.
- Mono/stereo 32/64-bit host buffers, float DSP, sample rate mismatch rejected.

## Tasks

1. [x] Portable host processor (`include/convolver/host.hpp`, `portable/host.cpp`)
   and `tests/native/host_tests.cpp`. Test direct convolution, planar/in-place,
   variable/zero chunks, invalid samples, latency-matched bypass/reset, prepared
   replacement and control-thread retirement. Observe failing tests, implement,
   run full suite. Add exported optional `Convolver::host` target and consumer test.
2. [x] Versioned embedded state codec and bounded corruption tests. Preserve
   configuration values, validate before publishing, reject truncation/oversize.
3. [x] Optional SDK build, processor/controller/factory and preset writer in
   `plugins/vst3`. Actual SDK-interface tests, SDK validator, standard preset
   round-trip; no SDK dependency in the ordinary core build. Test failure first.
4. [ ] CI matrix, docs, format/tidy, full FFTW/Accelerate and SDK suites; independent
   review and regression tests for important findings. Commit after local tests,
   fast-forward local main, publish CI branch from that exact commit if necessary
   to obtain CI before pushing main, then push main and check final CI.

## Evidence / decisions

Implemented host, state codec and SDK adapter. Initial API tests failed before
implementation; deterministic review regressions also failed before the fixes
for queued replacement starvation and preactivation tail reporting. The latter
reuses core validation to calculate the tail without FFT planning.
Local FFTW and Accelerate sanitizer gates and a ThreadSanitizer host run passed.
Actual module loading on this Mac is blocked by system policy rejecting ad-hoc
signatures; do not bypass it. CI must validate the loadable bundle on all three OSes.

Preset-first workflow confirmed by user. Compatibility
before speed confirmed; particular DAWs not specified. SDK test results must not
be described as actual commercial-DAW validation.
