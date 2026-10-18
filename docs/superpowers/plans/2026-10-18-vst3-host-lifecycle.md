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
4. [x] CI matrix, docs, format/tidy, full FFTW/Accelerate and SDK suites; independent
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

Independent final review found no remaining important issues. Linux and macOS
CI passed the actual SDK validator. Initial Windows CI compiled the plug-in but
failed module loading because SDK LoadLibraryW did not search its directory for
FFTW's DLL. The Windows plug-in recipe now uses static-md dependencies; ordinary
core/CLI CI continues testing dynamic Windows libraries.

Preset-first workflow confirmed by user. Compatibility
before speed confirmed; particular DAWs not specified. SDK test results must not
be described as actual commercial-DAW validation.

## Completed validation

Commit `5bcbef3` passed all seven CI jobs before publishing main:
- [Native and historical tests](https://github.com/jrp2014/convolver/actions/runs/39085968706): Linux Clang/GCC, Windows MSVC and macOS; package consumers, sanitizers, format/tidy and backend equivalence.
- [VST3 compatibility](https://github.com/jrp2014/convolver/actions/runs/39085968819): Linux, Windows and macOS, each loading the actual bundle with the SDK validator and checking the embedded preset workflow.

Local evidence: FFTW/Accelerate sanitizer suites 17/17 each; core-only package
5/5; SDK-interface/preset suites 11/11 (local bundle load excluded for the stated
OS-policy reason); standalone ThreadSanitizer host test passed. Independent
review's two important findings were fixed and verified with failing-then-passing
regressions. Commercial DAW and callback-deadline measurements remain future work.
