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
named-host qualification are done. Nothing has been published yet. After
publishing, jrp tells the people who already use the `.cfg` format where to
find it.

Contents: [candidates](#candidates) ·
[publishing an unsigned preview](#publishing-an-unsigned-preview) ·
[after publishing](#after-publishing-make-it-findable) ·
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

### Debugging symbols

Each candidate also uploads `symbols-<platform>`: the debugging symbols of the
packaged binaries (the VST3 plug-in, `convolver-preset` and
`convolver-equalizer-apo`), kept outside the package and its inventory
(DIAG-1, jrp's review of 24 October 2026). The candidate build sets
`CONVOLVER_DEBUG_SYMBOLS` ([build options](build.md#build-options)): Release
code is compiled with debug information, which does not change the generated
code, and the symbols are moved out of the binaries. `candidate.py` fails if
any packaged binary has none.

| Platform | Symbols | Matched by |
| --- | --- | --- |
| Linux | `<file>.debug` (`objcopy --only-keep-debug`); the binary is stripped and names it (`.gnu_debuglink`) | GNU build ID (`readelf -n`) |
| macOS | `<file>.dSYM` (`dsymutil`); the binary is stripped of its debug map and signed again ad hoc | Mach-O UUID (`dwarfdump --uuid`) |
| Windows | `<file>.pdb`, written there by the linker (`/DEBUG /OPT:REF /OPT:ICF`) | PDB signature (`dumpbin /headers`) |

The symbols match only binaries from the same run, so keep them with every
preview you publish (download them from the run that produced the candidate).
To symbolicate a crash report: on macOS, put the `.dSYM` beside the report and
use `atos -o Convolver.dSYM/Contents/Resources/DWARF/Convolver -l <load
address> <addresses>`, or let the crash reporter find it by UUID; on Linux, use
`gdb -s <file>.debug -e <binary>` or `eu-addr2line -e <file>.debug`; on
Windows, add the folder to the debugger's symbol path.

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
   contains. These are the files you will attach, unchanged. For the first
   preview, the independent walkthrough (REL-3 in [TODO](../../TODO.md)) uses
   these same files before they are uploaded.
3. Run the [smoke test](host-testing.md#smoke-test-for-an-unsigned-preview) in
   at least one host.
4. Create a GitHub pre-release on that tag titled "Convolver X.Y.Z (unsigned
   developer preview)". Put the SHA-256 sums, the run link and download and
   installation instructions (pointing to
   [unsigned previews](../use/install.md#unsigned-previews)) in the release
   notes.
5. Check it worked: download each asset from the published pre-release and
   compare it with `SHA256SUMS`, and read the release page as a user would.
   Never replace or edit an asset after publication; a fix gets a new version.

`production_enabled` stays false for a preview. Previews are unsigned on macOS
(ad-hoc signature; users remove the quarantine) and on Windows (SmartScreen
warns), as jrp decided.

## After publishing: make it findable

People who already use the `.cfg` format find Convolver through search, the
old SourceForge site and the forums where room correction is discussed. After
each preview is published, jrp does the following; each step needs repository
settings or jrp's own accounts:

1. In the [README](../../README.md), point a Download line at the latest
   release. With the first preview, also add build-status, licence and
   latest-release badges.
2. Check that the repository description, topics, Website field and
   [social preview](../../.github/social-preview.html) still list the formats
   and platforms the project offers; regenerate and upload the image if not.
3. Add a link to the release to the old [SourceForge project](https://sourceforge.net/projects/convolver/)
   files, keeping the 4.4 Windows download available for Windows Media Player
   users. Uploading the packages there instead is redistribution, so the
   source bundle goes with them ([licences](#licences-and-corresponding-source)).
4. Update the [KVR Audio listing](https://www.kvraudio.com/product/convolver_by_convolver_sf_net)
   (formats, platforms, version) and post the release in KVR's news.
5. For the first preview, and later for a preview that adds a format, post
   once in each community where `.cfg` users ask questions: Roon
   Community (DSP), AV Nirvana (REW and Audiolense), JRiver Interact, diyAudio
   (PC Based), Audio Science Review and Hydrogenaudio.
6. Once a release includes LV2, propose it to
   [awesome-linuxaudio](https://gitlab.com/nodiscc/awesome-linuxaudio).
7. Once CFG-1 has tested Roon, ask Roon to link the repository from its
   [convolution help page](https://help.roonlabs.com/portal/en/kb/articles/dsp-engine-convolution),
   which links only to the old site's format pages.

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
is not enough to change the policy. Draft signing scripts are kept in history
until signing is taken up (REL-1): `git show f4519022c:scripts/release/sign-macos.sh`
and `sign-windows.ps1`.

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
