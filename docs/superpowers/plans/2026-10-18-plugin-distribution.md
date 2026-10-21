# Downloadable and buildable Convolver plug-ins implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver tested VST3 downloads and source builds with usable documentation,
then extend the same release system to a macOS Audio Unit.

**Architecture:** Keep the C++20 DSP and host lifecycle independent of formats.
Add staging, dependency verification, packaging and release documentation around
the existing VST3 adapter. AU is a subsequent adapter project; it shares the core,
state semantics and tests, not the VST3 SDK interfaces.

**Tech stack:** CMake/CTest/CPack, C++20, FFTW, Accelerate, libsndfile, pinned VST3
SDK, GitHub Actions; Python standard library for release validation. Platform
signing/installer tools are release-time dependencies only.

**Spec:** [Plug-in distribution design](../specs/2026-10-18-plugin-distribution-design.md).

Status: proposed work, based on `0bf77d0` and portable package 0.2.0. This plan does
not implement a release or certify any commercial host. The existing
[configuration interoperability plan](../../archive/plans/2026-10-18-configuration-interoperability.md)
can proceed independently; its future APIs are not prerequisites here.

**Priority revised on 21 October 2026:** AU and LV2 are no longer follow-ons.
They are developed in parallel with VST3, sharing one release pipeline (PLUG-5);
see the [plug-in formats plan](2026-10-21-lv2-plugin.md).
**Signing decided on 21 October 2026:** first releases are unsigned previews on
macOS (ad-hoc signed, with documented quarantine removal) and Windows (SmartScreen
warning). Task 4's signing steps and the production acceptance below stay the
later gate; until then, milestone B is not claimed.
**Merged on 21 October 2026:** the `codex/plugin-distribution` implementation of
Tasks 1–3, the installer templates and candidate gates is on `main`. The two
signing scripts listed below stayed on that branch until signing is taken up.
The FFT choice below ("Windows/Linux use FFTW") is under review as PLUG-1: a
plug-in must not share an FFTW planner with other code in the host process.

## Global constraints

- Keep C++20; core CMake minimum 3.20, VST3 minimum 3.25.
- Preserve historical sources and documentation; use `jrp` in new author-facing copy.
- Compatibility comes before speed. No `-march=native` requirement in downloads.
- Preserve VST3 IDs, version-1 state, 256-sample latency and off-callback preparation.
- First release: mono/stereo and matching-rate embedded presets, with no implicit resampling.
- macOS downloads use Accelerate; Windows/Linux use FFTW. Keep both source options.
- No GUI framework, Python runtime, libsndfile or format SDK requirement in the DSP core.
- Publish only tested OS/architecture/host claims. Use the proposed matrix in the spec
  as candidates until Task 1 establishes the actual support record.
- Production downloads require licence/source completeness and final-artifact validation.

## Review focus

1. A clean machine has no build-tree libraries, SDK, compiler, FFTW installation or
   original IR assets: the installed plug-in and bundled tools must still work (Tasks 2–5).
2. A downloaded macOS archive has quarantine metadata, or Windows lacks the MSVC
   runtime: installation must follow the documented trust/prerequisite path (Tasks 4–5).
3. A saved project crosses machines, Unicode paths, architectures or plug-in upgrades:
   embedded state and stable identity must survive (Tasks 2 and 5).
4. Host rates/layouts change, or input is silent during a long tail: reject unsupported
   configurations clearly and preserve the continuous-stream contract (Tasks 5–6).
5. Source archives omit SDK submodules or rely on a mutable dependency checkout:
   the published build recipe must reproduce the tested components (Tasks 1 and 3).

## Delivery sequence

| Milestone | Included tasks | User-visible result | Exit gate |
| --- | --- | --- | --- |
| A: Buildable beta | 1–3, initial documentation from 6 | VST3 archives, converter, examples, pinned source recipe | Staged binaries and source-build smoke tests pass |
| B: Production VST3 | 4–7 | Signed installers, tested host instructions, release source bundle | Final downloaded artifacts pass installation and host gates |
| C: Audio Unit | Separate follow-on described below | Mac AU package using the same engine | AU validation plus Logic/SoundSource tests and Task 7 release gate |

Do not let additional formats, an editor or the full interoperability proposal
block milestones A/B. Do not label A as production merely because CI is green.

## File responsibilities

Existing files to extend: `CMakeLists.txt`, `CMakePresets.json`,
`plugins/vst3/CMakeLists.txt`, `plugins/vst3/preset.cpp`,
`tests/vst3/adapter_tests.cpp`, `tests/vst3/preset_tests.py`,
`.github/workflows/vst3.yml`, `README.md`, `CHANGELOG.md`, `docs/vst3.md`,
`docs/testing.md`, `docs/cmake-package.md` and `docs/modernization.md`.

Proposed new files:

| Path | Responsibility |
| --- | --- |
| `packaging/release-policy.json` | Schema-1 support matrix, dependency pins and expected artifact set |
| `cmake/ConvolverPluginPackaging.cmake` | Optional plug-in/tool/docs staging and CPack archive layout |
| `scripts/release/build_sources.py` | Assemble matching source/dependency archive from recorded pins |
| `scripts/release/verify_artifact.py` | Manifest, checksums, layout and runtime-dependency assertions |
| `scripts/release/sign-macos.sh` | Sign, notarize and staple staged macOS deliverables |
| `scripts/release/sign-windows.ps1` | Sign Windows binaries and installer using CI-provided identity |
| `packaging/windows/installer.nsi` | Optional NSIS installation, runtime prerequisite and uninstall rules |
| `packaging/macos/Distribution.xml` | macOS installer destinations and component selection |
| `tests/release/manifest_tests.py` | Release metadata/asset contract, including negative fixtures |
| `tests/release/package_smoke.py` | Relocated package and preset-converter checks |
| `tests/release/source_build.py` | Clean source-archive configure/build/test smoke |
| `tests/release/documentation_examples.py` | Execute maintained quickstart examples |
| `examples/plugin-presets/` | Generated, redistributable IR/config/preset examples and provenance |
| `docs/plugins/{index,install,quickstart,build,troubleshooting,hosts}.md` | Maintained reader-specific guide |
| `docs/releases/` | Release-specific matrix, test evidence, known issues and artifact/source manifest |
| `.github/workflows/release.yml` | Candidate assembly, verification and explicit publication |

Keep release tooling optional. Ordinary core configuration must neither fetch
dependencies nor require signing tools, NSIS or Python when tests/I/O are disabled.

## Task 1: Establish the release contract and pinned source builds

**Files:** release policy, source-build test, presets, build guide, changelog.

**Interfaces:** `release-policy.json` schema 1 records portable version, full source
commit, SDK commit/submodules, dependency versions/source hashes, compiler/runtime,
OS/architecture/backend and support status. Generate artifact manifests from this
policy and actual build results; do not copy divergent version strings into scripts.

- [ ] Record tested binary candidates from the spec. Choose the oldest advertised
  OS/CPU baseline only where a VM or physical test machine is available. Separate
  `tested`, `build-only` and `unverified` entries; record host/OS build numbers.
- [ ] Pin the VST3 SDK and all required submodules, FFTW/libsndfile sources and
  Windows vcpkg baseline/triplet. Resolve and record concrete checksums before a
  candidate build. Dependency acquisition is explicit, never an implicit core fetch.
- [ ] Add manifest tests that reject missing pins, conflicting versions, duplicate
  artifact names and an advertised architecture with no matching artifact.
- [ ] Add `release-vst3` configure/build/test presets with SDK path supplied through
  a user preset or `-D` override. Preserve existing core presets and option behavior.
- [ ] Document a complete source build on Windows, macOS and Linux, including the
  preset tool and test commands. Verify from a fresh directory with no CMake cache.
- [ ] Run `python tests/release/manifest_tests.py` and the source-build smoke on all
  three OS families; require exit 0 and real SDK adapter/validator tests. Commit.

**Acceptance:** A reader can build the exact candidate, and every claimed platform
and dependency is traceable. Coordinate proposed 0.3.0 numbering with other work
before changing `project(VERSION ...)`; retain the current same-minor package policy.

## Task 2: Stage relocatable VST3 archives and usable presets

**Files:** packaging CMake module, VST3/top-level CMake, package smoke test,
example directory, converter tests and install/quickstart drafts.

**Interfaces:** `cmake --install <build> --config Release --prefix <stage>
--component PluginDistribution` produces `VST3/Convolver.vst3`, `bin/` tools,
`presets/`, `docs/` and `licenses/`. This component is separate from SDK/library
exports. `verify_artifact.py --root <stage> --manifest <json>` exits nonzero for
missing content or forbidden dependencies. Preserve whole platform bundle layouts.

- [ ] Add a failing staging test for the missing distribution component. Assert
  both single-config and multi-config generators select the requested configuration.
- [ ] Install the actual SDK-produced bundle and `convolver-vst3-preset`; do not
  reconstruct bundle internals by guessing paths. Stage into a private directory,
  not the developer's real plug-in folder. Generate versioned ZIP/tar archives.
- [ ] Create identity and non-identity examples for mono/stereo at 44.1/48/96 kHz
  using synthetic, redistributable impulses. Record generation parameters and
  expected output; avoid unlicensed commercial room-correction data.
- [ ] Test conversion from WAV and legacy configuration, Unicode paths, no-overwrite
  protection, malformed inputs and explicit error text. If sample-rate/layout
  selection flags are needed, add them as a separate tested converter change rather
  than depending on an unimplemented inspection API.
- [ ] Copy staging to a new path containing spaces/Unicode, remove source IRs,
  generate/load a preset there and run the real SDK validator against that bundle.
  Assert no preset references the source tree and all examples produce known output.
- [ ] Run `ctest --test-dir <build> -C Release --output-on-failure`, then
  `python tests/release/package_smoke.py --root <relocated-stage>`; document install
  paths and commands that were exercised. Commit.

**Acceptance:** A beta user needs no compiler or source checkout. Archive copying
is supported; ordinary library `cmake --install` and external consumers still pass.

## Task 3: Close runtime dependencies and assemble corresponding source

**Files:** release policy/manifest, source assembler, artifact verifier,
source/package tests and build/licence documentation.

**Interfaces:** `build_sources.py --policy <json> --output <archive>` assembles
Convolver, dependency sources, notices and build scripts for that exact candidate.
`source_build.py --archive <archive>` builds from an isolated extraction.
Artifact manifests record source SHA, backend, architecture, minimum OS/libc,
dependency inventory and final-file hashes; no secret or local absolute path.

- [ ] Add failures for a bundle linked to a build-tree library, a missing dependency,
  an absent source archive and a mismatched dependency hash.
- [ ] On Windows, preserve static FFTW and the consistent MSVC runtime setting.
  Handle the supported redistributable through the installer or explicit prerequisite;
  never rely on the DAW's executable search path for third-party DLLs.
- [ ] On macOS, build and verify arm64 and x86-64 slices before making a universal
  deliverable. Use Accelerate in the plug-in; bundle compatible libsndfile dependencies
  for the converter, adjust loader-relative paths, then sign in Task 4.
- [ ] On Linux, build against the declared oldest libc baseline, check symbol versions
  and architecture, and use an explicit system-library allowlist. Bundle required
  non-system libraries with loader-relative paths; never copy the system libc.
- [ ] Inspect binaries using platform tools (`dumpbin`, `otool`/`lipo`, `readelf`),
  then run the installed artifacts on machines without the build dependencies.
  Metadata inspection alone is insufficient evidence of runtime compatibility.
- [ ] Include complete corresponding source/build materials and dependency notices
  with each binary release. Audit the exact GPL/LGPL/MIT/system-library combination;
  include relinking materials where required. A GitHub source snapshot alone omits
  externally fetched SDK/dependency content and is not the complete release recipe.
- [ ] Build from the source archive with pinned dependencies already supplied and
  network disabled during configure/build. Run numerical, adapter, validator and
  external-consumer tests. Verify packaged output and commit.

**Acceptance:** Both distribution routes are self-consistent and traceable;
performance tuning on the build machine cannot silently raise the CPU requirement.

## Task 4: Sign and package for ordinary installation

**Files:** platform signing scripts, Windows/macOS installer files, release workflow,
artifact verifier and installation/troubleshooting guide.

**Interfaces:** Signing scripts accept a staged directory and identity references
from CI environment/secret storage; private keys and passwords never enter source,
logs or artifacts. Unsigned builds are distinctly labelled developer previews.

- [ ] Test installer manifests against the actual stage: only owned files may be
  installed/removed; user-created presets and unrelated plug-ins must survive uninstall.
- [ ] Install Windows VST3 into the standard Common Files VST3 location, with tools,
  documentation and examples separate. Provide upgrade/uninstall behavior and an
  archive alternative. Pin the release-only NSIS tool and handle runtime prerequisites.
- [ ] Use Developer ID to sign nested macOS code and then bundles; build the package,
  submit with `notarytool`, check success, staple and assess the final deliverable.
  Run `codesign --verify --deep --strict` and `spctl` assessment as applicable.
- [ ] Authenticode-sign Windows code and installer with a trusted timestamp; verify
  with `signtool verify /pa /all`. Signing does not justify claiming that every
  SmartScreen/reputation warning will disappear.
- [ ] Download the produced packages through the user-facing route onto clean test
  machines, retaining normal quarantine/security behavior. Test install, host scan,
  preset loading, upgrade and uninstall without disabling operating-system security.
- [ ] Keep unsigned PR CI useful without access to secrets; restrict signing/publication
  to trusted release refs. If signing identities or minimum-OS machines are unavailable,
  finish the beta and mark the production gate incomplete. Document evidence and commit.

**Acceptance:** The actual downloadable artifacts install and load using the
documented workflow, without requiring users to bypass system security policy.

## Task 5: Validate real hosts and release behavior

**Files:** adapter/preset tests, package smoke tests, host guide and versioned
records under `docs/releases/`. Fix production code only for reproduced failures.

**Interfaces:** Each host record identifies host version, OS/build, architecture,
artifact hash, backend, rate/layout, steps, result and known limitations. Automated
SDK results, manual host tests and untested claims remain separate fields.

- [ ] Establish a release host matrix: REAPER on Windows/macOS/Linux as the common
  smoke host; Ableton Live and Cubase on Windows/macOS where licences/testers are
  available. Do not claim those additional hosts until an actual test passes.
- [ ] Exercise scan/load, identity and non-identity audio, variable block sizes,
  mono/stereo, 44.1/48/96 kHz, silence with long tails, stop/start/seek, bypass
  automation, multiple instances, filter changes and offline/bounce rendering.
- [ ] Save a project, delete/move its IR files, close/reopen it, and move it to another
  supported architecture. Repeat with a prior release's saved state after upgrade.
  Verify stable IDs, exact rate/layout errors and preservation of the last valid state.
- [ ] Compare rendered output to the independent convolution oracle, aligning by
  reported latency. Treat nonfinite output, incorrect tails or corrupted restore as
  blockers, even if the SDK validator passes.
- [ ] Measure callback time and allocation behavior using representative short and
  long IRs. Record host buffer, CPU and workload; do not turn an average-time result
  into a hard-real-time guarantee. Publish the supported envelope and observed limits.
- [ ] Convert every reproduced bug into a deterministic regression where possible,
  rerun affected hosts and full CI, then commit evidence and fixes.

**Acceptance:** Every advertised host/platform combination has named evidence;
untested combinations are identified, not inferred from the plug-in format alone.

## Task 6: Complete and test user/developer documentation

**Files:** `docs/plugins/` pages, README, maintained VST3/build/test guides,
documentation-example tests and generated example materials. Historical docs unchanged.

**Interfaces:** `docs/plugins/index.md` is the maintained landing page. Installation
and build paths both lead to the same quickstart; all packaged docs carry the release
version. Examples are executable fixtures, not separately maintained shell snippets.

- [ ] Provide download selection by OS/architecture, trusted installation locations,
  supported hosts, minimum versions, update/uninstall instructions and licence/source links.
- [ ] Provide a complete first-use walkthrough: identity preset, configure rate/layout,
  create a preset from WAV/config, load it, check levels, bypass and save/reopen a project.
  Explain embedded filters, latency, tails, single-precision DSP and unsupported layouts.
- [ ] Provide source instructions with pinned SDK submodules and dependency options,
  CMake presets, tests, staging and package commands. Include a core-only section
  retaining no mandatory Python/libsndfile/SDK dependency.
- [ ] Explain that ordinary players need a compatible plug-in host or routing solution.
  Include only tested host-specific recipes. Describe old WMP/DirectShow/VST2 support
  as historical; do not imply automatic migration of old plug-in identities or projects.
- [ ] Add troubleshooting for missing dependencies, scans/blacklists, OS signatures,
  architecture mismatch, wrong sample rate/layout, malformed presets and excessive gain.
  Describe safe recovery and how to submit a minimal configuration/log without private audio.
- [ ] Run `python tests/release/documentation_examples.py --package <stage>` on all
  supported platforms, check maintained local links, and run existing historical-document
  integrity tests. Have a tester follow the download and source routes without developer
  assistance. Commit documentation with the behavior it describes.

**Acceptance:** A new user can reach audible output by following the shipped guide;
an independent builder can reproduce the plug-in without undocumented local setup.

## Task 7: Publish immutable, tested release assets

**Files:** release workflow, release scripts, versioned evidence/manifest,
changelog, README and release links.

**Interfaces:** A candidate is identified by source commit and artifact hashes.
Publication promotes those exact tested assets to a GitHub Release; it does not
rebuild or replace them after validation. Signing precedes final checksums.

- [ ] Test failure gates: a missing architecture, failed host requirement, unsigned
  production package, failed notarization, absent source/notices or changed artifact
  hash must prevent production publication.
- [ ] Build from a protected, reviewed commit and immutable dependency pins. Produce
  native archives/installers, source bundle, checksums, licence inventory, instructions
  and a concise compatibility/known-issues report. Publish a beta first.
- [ ] Require numerical/sanitizer tests for FFTW and Accelerate, core-only and external
  consumers, historical integrity, SDK validation on all three OS families, source
  rebuilds, package installation and the advertised named-host matrix.
- [ ] Obtain signing/tester evidence and release authorization before publication.
  Review the complete candidate once; do not ask users to approve unbuilt deliverables.
- [ ] Publish the tested assets, download them again, verify checksums/signatures and
  a representative host load, and update maintained documentation to the immutable
  release URL. Retain previous releases for rollback and never overwrite their assets.
- [ ] Record commit, run URLs, artifacts and remaining limitations. The release is
  complete only when both download and source-build routes are verified.

**Acceptance:** An outside user receives the same binaries that passed the release
gate, with matching source and a usable path to report or recover from problems.

## Subsequent adapter projects

These need their own focused implementation plans after milestone B; the release
system above is reusable and must not be entangled with their SDKs.

1. **AUv2:** add `plugins/au/` and `tests/au/`, stable component IDs, versioned
   embedded state and AU presets mapping to the same host configuration. Decide
   preset conversion and generic-view loading before coding; VST3 preset files
   are not AU presets. Test `auval`, Logic Pro and SoundSource, covering persistence,
   supported layouts, latency/tail and downloaded signed installation. Package a
   `.component` alongside VST3, with the same core and Accelerate backend.
2. **Editor usability:** if preset conversion is a barrier, design a small accessible
   file-picker/status/gain UI with asynchronous loading. Choose native UI or a
   cross-format framework only after assessing maintenance, dependencies and licensing;
   do not rewrite the convolution engine to adopt a framework.
3. **CLAP/LV2:** add an adapter only for identified hosts/users. Reuse the core,
   state semantics, package/source workflow and oracle; implement format-specific
   lifecycle, latency, persistence and validation rather than relabelling a VST3 bundle.
4. **AAX or standalone/system-wide application:** scope separately if demand warrants
   vendor tooling/signing or audio-device/routing work. Neither is necessary for the
   first public plug-in release. AUv3/iPad is also a separate extension, not implied by AUv2.

## Effort, dependencies and handoff

Indicative engineering effort, excluding waiting for certificates, machines, testers
or host licences: beta packaging/source route **3–5 days**; dependency closure and
signing/installers **4–8 days**; host qualification/documentation/publication **3–6
days**, partly overlapping. A stable VST3 release is therefore plausibly **2–4 weeks**
of focused work, not a promised date. Re-estimate after Task 1. AU needs a separate
estimate after selecting and verifying the Apple SDK/preset integration.

The user-requested deliverable here is the plan. Begin implementation only after
scope review. Native implementation with focused independent reviews is a reasonable
default because packaging, manifests and tests share a tight contract; the existing
DSP does not need a parallel rewrite. Resolve signing identities and access to
minimum-OS/host test machines early, while allowing the buildable beta to progress.
