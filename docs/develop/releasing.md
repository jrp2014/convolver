<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Releasing

[Documentation index](../README.md) · [Build from source](build.md#pinned-preview-recipe) ·
[Host testing](host-testing.md) · [Status](../project/status.md)

For jrp and anyone preparing a release: how candidate packages are made, how an
unsigned preview is published, and what a production release still needs.

**In short:** the "Plug-in distribution candidates" workflow builds and tests
unsigned packages for four platforms with their corresponding source, and
cannot publish anything. Publishing a preview is a manual step owned by jrp:
run the workflow on a tag, check the files against their checksums and attach
them unchanged to a GitHub pre-release. Production releases stay disabled
(`production_enabled` is false) until signing, clean installation and
named-host qualification are done. Nothing has been published yet.

Contents: [candidates](#candidates) ·
[publishing an unsigned preview](#publishing-an-unsigned-preview) ·
[production gates](#production-gates) ·
[licences and corresponding source](#licences-and-corresponding-source)

## Candidates

The [release workflow](../../.github/workflows/release.yml) ("Plug-in
distribution candidates") downloads checksum-pinned dependencies, builds and
tests each platform with the [pinned preview recipe](build.md#pinned-preview-recipe),
and uploads unsigned packages with corresponding source. It has read-only
repository permissions. Version numbers come from the root `VERSION` file.

| Candidate | FFT backend | Minimum system (proposed, not yet verified) |
| --- | --- | --- |
| Windows x86-64 | Ooura (built in) | Windows 11 |
| macOS arm64 | Accelerate | macOS 14 |
| macOS x86-64 | Accelerate | macOS 14 |
| Linux x86-64 | Ooura (built in) | Ubuntu 22.04 (glibc 2.35) |

The two macOS archives are separate. A universal package waits until both
architectures have passed release qualification; a slice is not assumed to
work because it links.

Each run records the compiler, runtime choice, architecture, backend, system
build, runtime dependency inventory, source commit, source-archive checksum and
file hashes. The CI logs and artifact manifests are the authoritative record.
The source bundle holds the pinned SDKs, FFTW and libsndfile sources, the build
materials and notices, and rebuilds without fetching anything.

Which formats a package contains is decided by PLUG-5 in [TODO](../../TODO.md);
the pins are in [`release-policy.json`](../../packaging/release-policy.json)
and [build from source](build.md#pinned-format-dependencies).

## Publishing an unsigned preview

Signing is not needed for a preview. Publication is a manual step owned by
jrp, and has not been done yet:

1. Run the "Plug-in distribution candidates" workflow by hand on the chosen tag
   (`vX.Y.Z`): Actions, then "Run workflow", choosing the tag, or
   `gh workflow run release.yml --ref vX.Y.Z`. The version bot's tag push does
   not start it, because pushes made with the workflow token do not trigger
   other workflows. Wait for the whole run to pass.
2. Download that run's `complete-preview-candidate` artifact (made by its
   `collect` job) and check every file against the `SHA256SUMS` file it
   contains. Publish exactly those files, unchanged. For the first preview,
   the independent walkthrough (REL-3 in [TODO](../../TODO.md)) uses these same
   files before they are uploaded (owner's decision, 23 October 2026).
3. Run the [smoke test](host-testing.md#smoke-test-for-an-unsigned-preview) in
   at least one host.
4. Create a GitHub pre-release on that tag titled "Convolver X.Y.Z (unsigned
   developer preview)". Put the SHA-256 sums, the run link and download and
   installation instructions (pointing to
   [unsigned previews](../use/install.md#unsigned-previews)) in the release
   notes.
5. Never replace or edit an asset after publication; a fix gets a new version.

`production_enabled` stays false for a preview. Previews are unsigned on macOS
(ad-hoc signature; users remove the quarantine) and on Windows (SmartScreen
warns), as jrp decided on 21 October 2026.

## Production gates

`production_enabled` is false in the [release policy](../../packaging/release-policy.json).
A production release needs every one of these, with evidence bound to the
final file hashes:

- Apple Developer ID Application and Installer identities, notarization and
  final assessment.
- Windows Authenticode identities, trusted timestamps and a signed embedded
  uninstaller.
- Clean installation from a download, upgrade and uninstall, on machines at the
  minimum system versions.
- The [production qualification](host-testing.md#production-qualification) in
  named hosts, projects moved between architectures, and state saved by the
  previous release restored.
- Callback timing under recorded workloads, and an independent tester's
  walkthrough.
- A final dependency and licence review, and jrp's explicit authorization.

Promotion also needs every platform's files, matching sources and notices, and
unchanged final hashes. Signing comes before the final hashes. A passing CI run
is not enough to change the policy. Developer signing scripts exist only on the
old `codex/plugin-distribution` branch until signing is taken up (REL-1).

## Licences and corresponding source

Convolver, and FFTW when a build selects it, are GPL-2.0-or-later; the Audio
Unit is distributed under GPL-3.0-or-later because of AudioUnitSDK's Apache-2.0
licence ([Audio Unit licence](../use/plugins.md#audio-unit-licence));
libsndfile is LGPL-2.1-or-later; the pinned VST3 SDK is MIT; the built-in Ooura
FFT has a permissive notice, shipped as `licenses/Ooura.txt`. Packages include
the upstream author and licence files, and FFTW's notice only in builds that use
it.

Preview libraries are static and libsndfile's optional codecs are disabled. The
complete source and build scripts allow rebuilding and relinking with modified
libraries. When redistributing, do not add proprietary components or leave out
the corresponding materials. A production review must examine the exact
shipped combination, including installer and runtime notices.

Upstream terms: [FFTW](https://www.fftw.org/faq/section1.html#isfftwfree),
[libsndfile](https://libsndfile.github.io/libsndfile/),
[Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
and [Microsoft SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).

The 18 October 2026 [developer-preview record](../archive/releases/2026-10-18-developer-preview.md)
keeps the original evidence, including the local signing limitations found
then.
