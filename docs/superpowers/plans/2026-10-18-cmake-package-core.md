# Independent core and CMake package implementation plan

**Goal:** deliver milestone 1 of [the structure review](../../project-structure.md):
independent core builds, exported targets, presets and external-consumer tests.
The user has authorized implementation, testing, committing and pushing.

**Architecture:** retain the full-build default and historical NATIVE switch;
make I/O and CLI optional. Ship static `Convolver::core` and `Convolver::io`
targets, with component-specific dependency discovery. Keep DSP behavior intact.

**Constraints:** CMake 3.20, C++20, native FFTW/Accelerate, no downloads during
configure, no Python/libsndfile needed for core-only builds including core tests.
Portable package version starts at 0.1.0; historical release versions are unchanged.
Presets use schema 2 and Ninja. Shared-library/ABI promises remain out of scope.

**Execution:** implement here on `codex/cmake-package-core`; the existing checkout
has no tracked edits, so a branch suffices without moving the user's scratch data.
Commit after local verification, push the feature branch to obtain CI evidence,
then fast-forward and push main only after CI passes.

## Tasks and evidence

- [x] Add `tests/consumer` using only public headers and namespaced targets.
  Verify it fails against the existing project/package. Exercise a direct
  convolution oracle and an optional real-file round trip.
- [x] Split CMake build choices and dependency modules. Core-only builds and
  source-tree consumers must configure with Python and libsndfile discovery
  disabled. Subproject use must not enable Convolver's tests by default.
- [x] Install component exports, package/version files and dependency finders.
  A full installation must support core-only consumers without sndfile lookup;
  core-only installations must reject required I/O and unknown components.
- [x] Add a CMake-driven CTest integration test that installs to a temporary
  prefix, moves that prefix, configures/builds/runs a separate consumer, and
  checks repeated component lookup and optional/missing components.
- [x] Add release, sanitizer, core-only and Accelerate presets; exercise presets
  locally and add core-only/consumer coverage to Linux, Windows and macOS CI.
- [x] Update build/package/contribution documentation and the milestone status.
- [x] Run full native/legacy suites, dependency-isolation and relocation tests;
  review the complete change.
- [ ] Obtain green branch CI, then fast-forward main and verify its CI.

## Review focus

Dependency targets must be recreated without embedded developer paths. Static
FFTW/thread dependencies and sanitizer runtimes must reach consumers. Optional
I/O must not leak into core consumption. Subproject defaults must not mutate the
parent's test policy. Multi-config Windows builds and toolchain arguments must
survive nested consumer tests; installed targets must tolerate prefix relocation.

## Verification record

Local verification on Apple M5 Max / AppleClang 21, 18 October 2026:

- FFTW 3.3.11 NEON release and sanitizer presets: 15/15 tests each.
- Accelerate release and sanitizer presets: 15/15 tests each.
- Core-only FFTW preset: 3/3, including installed/source consumer verification.
- Consumer tests cover relocation, component availability, repeated lookup and
  parents with/without CTest. Review found that unconditional `include(CTest)`
  changed parent policy; a failing regression test confirmed it, and the fix
  passed that test and all five preset suites.
- Windows-style relative IR/list paths failed before the normalization fix and
  pass afterwards, including nested and non-ASCII paths. Non-Windows drive/rooted
  paths receive an actionable error rather than a guessed filesystem mapping.
- clang-format 18 checks pass. clang-tidy 21 reports no project diagnostics;
  the local macOS 27 SDK requires this newer parser. CI uses Linux clang-tidy 18.
- Independent review found no additional important issues. Historical source and
  documentation inventories remain unchanged.

Scope clarified by the user: defer PCM16 dither to the next audio compatibility
change; include Windows path compatibility, formatting and static analysis now.
GitHub CI will verify Linux and Windows as well as the macOS configurations before
main integration. Development packages do not bundle external runtime libraries.
