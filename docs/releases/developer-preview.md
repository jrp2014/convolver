<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Portable developer-preview release record

Author: jrp. Started 18 October 2026. This records implementation and unfulfilled
qualification gates; it is not a production release announcement.

## Update, 21 October 2026

This work was merged into `main` (it was previously only on the
`codex/plugin-distribution` branch). The owner decided that the first releases
are unsigned on macOS (ad-hoc signature, documented quarantine removal) and
Windows (SmartScreen warning). The signing gates below stay open for a later
production release. The plug-in now accepts 1–8 input and 1–8 output channels
(0.4.0) and is also tested with pluginval; the example presets still cover mono
and stereo. The shared release pipeline for VST3, LV2 and AU (PLUG-5 in
[TODO](../../TODO.md)) grows from this work, and the plug-in FFT backend choice
(PLUG-1) was settled the same day: Ooura on Linux and Windows, Accelerate on
macOS, as the matrix below now shows. FFTW remains pinned for builds that select
it, and its notice ships only in those. The developer signing
scripts were left on the branch until signing is taken up.

## Candidate matrix

| Candidate | Backend | Automated record | Minimum OS / named host |
| --- | --- | --- | --- |
| Windows x86-64 | Ooura (built in) | CI artifacts bind source/hash/toolchain | Unverified; Windows 11 proposed |
| macOS arm64 | Accelerate | CI artifacts bind source/hash/toolchain | Unverified; macOS 14 proposed |
| macOS x86-64 | Accelerate | CI artifacts bind source/hash/toolchain | Unverified; macOS 14 proposed |
| Linux x86-64 | Ooura (built in) | CI artifacts bind source/hash/toolchain | Unverified; Ubuntu 22.04 / glibc 2.35 proposed |

Native macOS archives are separate. A universal package is deferred until both
architectures have passed the full release qualification; no slice is assumed
compatible solely because it links.

Each candidate run records actual compiler, runtime choice, architecture, backend,
OS build, runtime dependency inventory, source commit, source-archive checksum and
file hashes. CI logs and artifact manifests are the authoritative automated record.
The source bundle contains all pinned SDK submodules, FFTW and libsndfile sources,
Convolver build materials and notices. It can rebuild without dependency fetching.

## Local evidence and limitations

Baseline core/consumer/historical tests passed 18/18. Source/archive and package
checks exercise real libraries and synthetic presets. A local signature ordering
regression was reproduced: the SDK generated `moduleinfo.json` after sealing the
bundle. Final sealing now follows metadata and verification covers both assembled
and relocated bundles.

The local machine also rejects ad-hoc or unknown signing chains. Bundle loading
was stopped after user-reported popups. Earlier exit-0 SDK calls are **not** trusted
local signing or installation evidence. Local subsequent tests disable SDK bundle
loading and moduleinfo loading; CI runs the actual validator, requiring a nonempty
successful test summary. Security controls were not bypassed.

## Remaining production gates

- Developer ID Application/Installer identities, notarization and final assessment.
- Authenticode identities, trusted timestamps and signed embedded uninstaller.
- Clean downloaded installation, upgrade/uninstall and minimum-OS machines.
- Named-host procedure, cross-architecture projects and prior-release state restore.
- Callback timing under recorded workloads and independent tester walkthrough.
- Final dependency/licence review and explicit release authorization.

`production_enabled` is false in the policy. A production promotion requires all
platform assets, matching sources/notices, unchanged final hashes, signatures and
hash-bound evidence for every required gate. Signing precedes final hashes. A
passing CI run is insufficient to change that policy.

## Licence and reproducibility notes

Convolver (and FFTW, when selected) use GPL-2.0-or-later; libsndfile uses
LGPL-2.1-or-later; the pinned VST3 SDK uses MIT; the built-in Ooura FFT has a
permissive notice, shipped as `licenses/Ooura.txt`. Notices include upstream author/licence files. Preview libraries
are static and optional libsndfile codecs are disabled. Complete source and build
scripts permit rebuilding/relinking with modified libraries; do not add proprietary
components or omit corresponding materials when redistributing. Production review
must examine the exact shipped combination, including installer/runtime notices.

See upstream [FFTW terms](https://www.fftw.org/faq/section1.html#isfftwfree),
[libsndfile licence](https://libsndfile.github.io/libsndfile/),
[Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
and [Microsoft SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).

## Candidate workflow

The candidate workflow downloads checksum-pinned dependencies, builds and tests
four native platforms, and uploads unsigned preview artifacts with corresponding
source. It has read-only repository permissions and cannot publish releases.
Version numbers come from the same root `VERSION` file as the portable package.
No production release or merge into `main` is part of this work.

## Publishing an unsigned preview (planned, not yet done)

Signing is not a prerequisite for a preview; the production gates above are
separate. Publication is a manual step owned by jrp:

1. Choose one green candidate run on a tagged commit (`vX.Y.Z`).
2. Download its `collect` artifact and check every file against the recorded
   SHA-256 sums. Publish exactly those files, unchanged.
3. Create a GitHub pre-release on that tag titled "Convolver X.Y.Z (unsigned
   developer preview)". Put the SHA-256 sums, the run link and download and
   installation instructions (pointing to the install guide's unsigned-preview
   section) in the release notes.
4. Never replace or edit an asset after publication; a fix gets a new version.

Before publishing, run the smoke test in the
[host qualification guide](../plugins/hosts.md#smoke-test-for-an-unsigned-preview)
in at least one host. `production_enabled` stays false.
