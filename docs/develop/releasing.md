<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Releasing

[Documentation index](../README.md) · [Build from source](build.md#preview-recipe) ·
[Host testing](host-testing.md) · [Status](../project/status.md)

For jrp and anyone preparing a release: how candidate packages are made, how an
unsigned preview is published, and what a production release still needs.

**In short:** the "Plug-in distribution candidates" workflow builds and tests
unsigned packages for four platforms with their corresponding source, and
cannot publish anything. Publishing a preview is a manual step owned by jrp:
run that workflow on a tag, then the "Draft preview release" workflow, which
checks the files against their checksums and attaches them unchanged to a
draft GitHub pre-release that jrp reads and publishes. Production releases stay disabled
(`production_enabled` is false) until signing, clean installation and
named-host qualification are done. Nothing has been published yet. After
publishing, jrp tells the people who already use the `.cfg` format where to
find it.

Contents: [candidates](#candidates) ·
[what a package holds](#what-a-package-holds) ·
[responsiveness qualification](#responsiveness-qualification) ·
[publishing an unsigned preview](#publishing-an-unsigned-preview) ·
[after publishing](#after-publishing-make-it-findable) ·
[production gates](#production-gates) ·
[licences and corresponding source](#licences-and-corresponding-source)

## Candidates

The [release workflow](../../.github/workflows/release.yml) ("Plug-in
distribution candidates") downloads the newest release of each dependency into
one source bundle, recording each version, commit and checksum, then builds and
tests each platform from that bundle with the [preview recipe](build.md#preview-recipe),
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
The source bundle holds the SDK, FFTW and libsndfile sources it was built from
(its `source-manifest.json` records each release, commit and checksum), the
build materials and notices, and rebuilds without fetching anything; it holds
every SDK in the policy, AudioUnitSDK included, whichever packages use it. The
candidate record names the NSIS release that made the Windows installer, and
the plug-in formats its package holds (the policy's `formats`).

### What a package holds

Every package holds the VST® 3 plug-in (`VST3/Convolver.vst3`), the tools in
`bin/` (`convolver`, `convolver-preset`, `convolver-equalizer-apo`), the
example presets in `presets/`, the first-use examples in `examples/first-use/`,
every user guide in `docs/use/` with its images in `docs/use/images/`, licence
notices in `licenses/` and `VERSION.txt`. The two macOS packages also hold the
Audio Unit, `Components/Convolver.component` (AU-5), with its own notices
inside it and in `licenses/` (`Convolver-AU.txt`, `GPL-3.0.txt`,
`audiounitsdk.txt`). LV2 joins a later preview.

The macOS installer (`…-unsigned.pkg`) has three packages, each with a
permanent receipt identity in
[`identities.json`](../../plugins/identities.json): the VST3 plug-in into
`/Library/Audio/Plug-Ins/VST3`, the Audio Unit into
`/Library/Audio/Plug-Ins/Components`, and everything else, with
`uninstall.sh`, into `/Library/Application Support/Convolver`. The Windows
installer (`…-unsigned-setup.exe`) puts the plug-in in the all-users VST3
folder and the rest in the folder chosen.

The VST trademark wording jrp approved, and Steinberg's VST Compatible logo,
are in the README, every packaged guide that mentions VST, the VST3 bundle's
`Contents/Resources/NOTICE.txt` and the macOS installer's text; their one
source and the rules are in [trademarks](../../packaging/trademarks/README.md).

What the candidates check automatically, on the runner that built each one:

| Check | What it shows |
| --- | --- |
| `release.stage` and `package_smoke.py` (staged, then after the ZIP is unpacked into a folder with spaces and `ü`) | Every file above is present; the tools run from the moved folder and reproduce the shipped presets and first-use renders; the VST3 validator and probe load each example preset; on macOS both bundles' signatures hold and the component passes `au.bundle`'s checks (identities, version, one exported symbol, macOS libraries only, notices); the logo is Steinberg's file and each guide that mentions VST shows it from inside the package; the plug-in's credits carry the wording |
| `verify_artifact.py` | The file inventory and checksums, the architecture, macOS 14 or glibc 2.35 as the floor, no libraries beyond the system's in any binary (the component's included), the corresponding-source archive, and the required notices for the platform's formats |
| `symbols.py` | Each packaged binary's [debugging symbols](#debugging-symbols) match it |
| `macos_installer.py` (macOS only) | The `.pkg` installs exactly the ZIP's files in the places above, byte for byte, with three receipts; the installed bundles' signatures hold; the installed Audio Unit passes `au.bundle`'s checks and `auval -strict`, which also lists its window; the installed CLI runs; `uninstall.sh` removes every installed file |
| `windows_installer.py` (Windows only) | Upgrade and locked-file recovery of the NSIS installer |

These are package checks on CI runners. They do not replace a clean
installation from a download on a Mac or PC that never had Convolver (with
the quarantine and SmartScreen warnings a user meets), the named-host smoke
tests (HOST-1, AU-4) or the independent walkthrough (REL-3).

### Debugging symbols

Each candidate also uploads `symbols-<platform>`: the debugging symbols of the
packaged binaries (the VST3 plug-in, the Audio Unit on macOS, the `convolver`
CLI, `convolver-preset` and `convolver-equalizer-apo`), kept outside the
package and its inventory
(DIAG-1, jrp's review of 24 October 2026). The candidate build sets
`CONVOLVER_DEBUG_SYMBOLS` ([build options](build.md#build-options)): Release
code is compiled with debug information, which does not change the generated
code, and the symbols are moved out of the binaries. After installing the
package, `candidate.py` checks each packaged binary against its symbols
([`symbols.py`](../../scripts/release/symbols.py), tested by `release.symbols`):
the symbols must exist, hold debugging information and carry the binary's
identity in the table below. It fails, naming each problem, if any binary's
symbols are missing, empty, without debugging information or from another
build. The artifact's `README.txt` lists the identity of each file. Both
plug-ins' executables are called `Convolver`, so the Audio Unit's symbols are
`Convolver-au.dSYM` (its DWARF file inside is still `Convolver`) and the
VST3 plug-in's `Convolver.dSYM`; the CLI's are `convolver-cli.*`.

| Platform | Symbols | Matched by | Debugging information checked |
| --- | --- | --- | --- |
| Linux | `<file>.debug` (`objcopy --only-keep-debug`); the binary is stripped and names it (`.gnu_debuglink`) | GNU build ID (`readelf -n`), and the `.gnu_debuglink` name and CRC-32 | `.debug_info` not empty |
| macOS | `<file>.dSYM` (`dsymutil`); the binary is stripped of its debug map and signed again ad hoc | Mach-O UUID of each architecture (`dwarfdump --uuid`) | `__DWARF,__debug_info` in `Contents/Resources/DWARF/<file>` not empty |
| Windows | `<file>.pdb`, written there by the linker (`/DEBUG /OPT:REF /OPT:ICF`) | PDB name, GUID and age in the binary's CodeView record (`dumpbin /headers`) and in the PDB | DBI stream lists compiled modules |

The symbols match only binaries from the same run, so keep them with every
preview you publish (download them from the run that produced the candidate).
To symbolicate a crash report: on macOS, put the `.dSYM` beside the report and
use `atos -o Convolver.dSYM/Contents/Resources/DWARF/Convolver -l <load
address> <addresses>`, or let the crash reporter find it by UUID; on Linux, use
`gdb -s <file>.debug -e <binary>` or `eu-addr2line -e <file>.debug`; on
Windows, add the folder to the debugger's symbol path.

Which formats a package contains is the `formats` of each platform in the
release policy (above); where each dependency comes from is in
[`release-policy.json`](../../packaging/release-policy.json), and how builds
choose and record releases in [build from source](build.md#supporting-libraries).

## Responsiveness qualification

CI's timing tests run on shared runners. When they cannot judge (a late
baseline, a thread that woke late) they report "not qualified", and CTest
shows the test as skipped. A green CI run therefore does not show that
loading a filter leaves the host responsive. Before a release, jrp (or
whoever he names) qualifies it on a named machine (REL-6 in
[TODO](../../TODO.md), from jrp's review of `db54160`):

1. **The machine.** Record its model, processor, memory and OS version.
   Run on mains power with Low Power Mode off and nothing else open. The
   first named machine is jrp's MacBook Pro, through the command line.
2. **The source.** Check out the candidate's tag. Fetch the SDKs the
   candidate used from its `source-manifest.json` (in the candidate's
   source bundle; [repeating a recorded build](build.md#supporting-libraries)).
   If the manifest has no `audiounitsdk` entry (candidates without the Audio
   Unit), fetch that one with `--latest` and record its version. The timing tests
   build each adapter from that source into their own executables, as the
   candidate did, rather than loading the packaged files; the
   [smoke test](host-testing.md#smoke-test-for-an-unsigned-preview) covers
   those files.
3. **The build.** Optimised, without sanitizers: in any other build the
   tests only report. On macOS:

   ```sh
   python3 scripts/release/sources.py --lock source-manifest.json --fetch \
     --cache build/downloads --destination build/sources \
     vst3sdk vst3-base vst3-cmake vst3-pluginterfaces vst3-public-sdk audiounitsdk
   cmake -S . -B build-qualify -DCMAKE_BUILD_TYPE=Release \
     -DCONVOLVER_BUILD_VST3=ON -DCONVOLVER_VST3_SDK="$PWD/build/sources/vst3sdk" \
     -DCONVOLVER_BUILD_AU=ON -DCONVOLVER_AU_SDK="$PWD/build/sources/AudioUnitSDK"
   cmake --build build-qualify --parallel
   ```

4. **The runs.** First from an empty instance, then from a full history
   (`--warm`, which takes some minutes with the 30 s filter):

   ```sh
   ctest --test-dir build-qualify -L timing --output-on-failure
   build-qualify/plugins/vst3/convolver_vst3_lifecycle --load-timing qualify-vst3-warm --warm
   build-qualify/plugins/au/convolver_au_lifecycle --load-timing qualify-au-warm --warm
   ```

5. **The verdict.** The limits are those in
   [load timing](load-timing.md) and are not relaxed here. The
   release qualifies only if every entry in every run printed "passed" or
   "reported only". An entry that failed, or was "not qualified", means the
   release is not qualified on that machine. That stays true even if a
   rerun passes: each test already gives an entry up to three runs and keeps
   every one. The "warm first" and "warm second" phases are shown, not
   judged.
6. **The evidence.** Keep every `*-load-timing.json` file (each names the
   version, build, compiler, FFT backend and machine), the printed tables
   and the machine record, with the candidate's run link, in the project's
   release evidence. State the result in the pre-release notes: qualified,
   naming the machine, or not qualified, naming the entries that did not
   pass.

## Publishing an unsigned preview

Signing is not needed for a preview. Publication is a manual step owned by
jrp, and has not been done yet. The "Draft preview release" workflow
([`publish.yml`](../../.github/workflows/publish.yml), REL-8) does the
mechanical part, but only as a draft: nothing is public until jrp presses
**Publish**.

1. Run the "Plug-in distribution candidates" workflow by hand on the chosen tag
   (`vX.Y.Z`; the first preview comes from a 0.12.x tag): Actions, then
   "Run workflow", choosing the tag, or
   `gh workflow run release.yml --ref vX.Y.Z`. The version bot's tag push does
   not start it, because pushes made with the workflow token do not trigger
   other workflows. Wait for the whole run to pass (about 25 minutes, most of
   it waiting for macOS runners).
2. For the first preview, the independent walkthrough (REL-3 in
   [TODO](../../TODO.md)) uses that run's files before anything is uploaded:
   the `candidate-macos-arm64` or `candidate-macos-x86_64` artifact, or
   `complete-preview-candidate` (made by the `collect` job), whose
   `SHA256SUMS` lists every file.
3. Run the [smoke test](host-testing.md#smoke-test-for-an-unsigned-preview) in
   at least one host, and the
   [responsiveness qualification](#responsiveness-qualification) on a named
   machine; the release notes give its result either way.
4. Run "Draft preview release" from `main` (Actions, then "Run workflow"),
   giving the candidate run's ID (the number in its address) and the
   responsiveness result in one sentence, or
   `gh workflow run publish.yml -f run_id=<ID> -f qualification="<result>"`.
   It refuses a run that was not started by hand on a `vX.Y.Z` tag, did not
   pass, or whose tag holds another commit or `VERSION`; it checks every file
   against `SHA256SUMS` and the candidate record, and refuses if a release or
   draft for the tag already exists. It then creates a draft pre-release on
   the tag, titled "Convolver X.Y.Z (unsigned developer preview)", with only
   the macOS files: for arm64 and for x86-64, the ZIP archive and the
   `-unsigned.pkg` installer, with the
   [corresponding source](#licences-and-corresponding-source)
   (`Convolver-source.tar.gz`), as the first preview is macOS-only
   ([decisions](../project/decisions.md#plug-in-behaviour)). Its notes give
   the SHA-256 of each attached file, the run link, the source commit, links
   to the [install guide](../use/install.md#unsigned-previews) at the tag,
   the known limitations, how to
   [report a problem](../use/troubleshooting.md#report-a-problem) (Copy
   Diagnostics, the host and its version, how the filter was loaded, and
   whether playback, export or reopening is affected) and the
   responsiveness result. Finally it downloads
   every attached file again and compares it with its sum. The run's summary
   shows the notes and links to the draft. A pull request that changes the
   workflow or its scripts runs a dry run on the newest candidate run on a
   tag: every check and the notes, but no draft.
5. Read the draft as a user would, edit its notes if needed (a draft's notes
   can change; its files must not), then press **Publish release**. To start
   again, delete the draft and run step 4 again. Never replace or edit an
   asset after publication; a fix gets a new version.
6. Download one asset from the published page and compare it with its line
   in the notes, and download the candidate run's `symbols-macos-arm64` and
   `symbols-macos-x86_64` artifacts and keep them
   ([debugging symbols](#debugging-symbols)): artifacts expire after 90 days.
   Then record the published version in the
   [changelog](../../CHANGELOG.md): a heading for the tag above the entries it
   contains. That pull request's merge bumps `VERSION` again, which is
   expected: the tag, not `main`, is what was published.

Without the workflow (for example while it is being fixed), the same steps by
hand: download `complete-preview-candidate`, run `sha256sum -c SHA256SUMS`,
and create the pre-release on the tag with the five files above and notes
giving their sums, the run link and the install guide.

`production_enabled` stays false for a preview. Previews are unsigned on macOS
(ad-hoc signature, because Apple silicon will not load code with no signature
at all; users remove the quarantine) and on Windows (SmartScreen
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
libsndfile is LGPL-2.1-or-later; the VST3 SDK is MIT; AudioUnitSDK is
Apache-2.0 (`licenses/audiounitsdk.txt` in macOS packages); the built-in Ooura
FFT has a permissive notice, shipped as `licenses/Ooura.txt`. Packages include
the upstream author and licence files, and FFTW's notice only in builds that use
it. The VST Compatible logo is Steinberg's, from the VST 3 SDK's documentation
(MIT), used under Steinberg's
[usage guidelines](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST%2B3%2BLicensing/Usage%2Bguidelines.html)
([trademarks](../../packaging/trademarks/README.md)).

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
