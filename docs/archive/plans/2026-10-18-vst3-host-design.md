> **Archived plan — completed.** Moved on 21 October 2026 from `docs/superpowers/specs/2026-10-18-vst3-host-design.md`. Design spec for the [host lifecycle plan](2026-10-18-vst3-host-lifecycle.md), implemented in `2f44142` and `5bcbef3`. Current behavior: [VST3 guide](../../vst3.md). The text below records intent and evidence at that time; its status lines, checkboxes and test counts were not updated. For current behavior read the guides, not this plan.

# Host lifecycle and VST3 compatibility

The user authorizes implementation, tests, commit, merge to local main before
push, and CI fixes. Compatibility takes priority over speed. Initial filter
loading uses DAW-loadable presets with embedded coefficients; no custom editor.
macOS and Windows are primary; Linux remains tested. No particular DAW is assumed.

## Design

Keep the existing Engine and CLI contracts. Add a reusable host layer linked to
core, without file access or SDK dependencies. Prepare engines and storage on the
control thread. Process planar float/double host buffers through preallocated
float scratch; support in-place buffers, silent/missing channels, variable and
zero frame counts. Double host samples are converted to float; this is not a
double-precision DSP promise. Sanitize nonfinite/out-of-float-range input, contain
nonfinite output, and make the callback nonthrowing. Preserve a fixed 256-frame
partition latency independently of host callback size.

Provide a bounded single-producer/single-consumer prepared-engine handoff. Warm
new state for one latency period, crossfade for 256 samples, and retire old
engines for control-thread destruction. Coalesce pending replacements; no heap
allocation, lock, planning, file I/O or destruction on the callback. Separate
control calls must be serialized by the caller; teardown requires processing
stopped. Bypass uses a 64-sample ramp and latency-matched dry audio while wet state
continues to advance. Start/stop/reset clears history without allocation.

Add a thin optional VST3 processor/controller using pinned SDK 3.8.1, outside the
core package. Negotiate mono/stereo equal input/output buses, float/double buffers,
real-time/offline processing; handle parameter-only calls and sample-offset bypass
automation. State embeds versioned, bounded filter data, preserving gain/routing/
delays. Reject malformed state transactionally and mismatched sample rates rather
than silently changing filter timing. Fixed latency avoids host compensation
changes on filter replacement. No native VST2 project migration claim.

A companion preset tool reads existing IR/config/list formats and writes standard
.vstpreset files. Host projects persist the embedded state, not original paths.
Plugin builds use PIC, bounded resources, ordinary portable compiler flags, and
explicit SDK location (no downloads during configure). Core builds retain CMake
3.20; optional SDK build requires 3.25. Developer bundles need external FFTW at
runtime; release signing/installers/dependency bundling are separate work.

## Acceptance

Direct numerical oracles; float/double, variable chunks, aliasing, silence/tails,
bypass, restarts, invalid samples, concurrent replacement, lifetime/allocation
checks, state corruption and relocation. Build the real plugin and run Steinberg's
validator plus adapter tests using SDK interfaces on Linux/macOS/Windows. Exercise
FFTW and Accelerate locally. Keep all existing CLI/core/package/archival tests.
Document actual DAW tests as unperformed unless executed; no hard real-time or
universal-host compatibility claim from validator success alone.
