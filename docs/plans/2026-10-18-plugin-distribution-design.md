# Convolver plug-in distribution design

Status: proposed release scope, 18 October 2026. This describes future work;
it does not claim that production installers, Audio Units or commercial-host
certification already exist.

**Current status (21 October 2026, `4e1bb1b`):** unsigned VST3 preview
artifacts are built by the merged candidate workflow; nothing is published yet.
Plug-in builds use Ooura on Windows and Linux and Accelerate on macOS (PLUG-1),
not FFTW as stated below. First releases are unsigned previews, and LV2 and AU
join the same pipeline. See the
[plug-in formats plan](2026-10-21-plugin-formats.md) for the current plan;
the rest of this page is the 18 October design record.

## Outcome and audience

People should be able to download, install and use Convolver without a compiler,
or build equivalent plug-ins from documented source. Serve Windows and Mac users
first, including people applying room/headphone correction through a plug-in host.
Provide a Linux build and clearly distinguish supported binary platforms from
additional platforms on which the portable source may build.

The user has chosen compatibility over maximum speed, C++20, CMake, standard
library/POSIX facilities where practical, and presets before a custom editor.
Historical Windows integrations and recovered documentation remain preserved.

## Existing foundation

At source commit `0bf77d0`, the repository has independently buildable core, host
and I/O libraries; FFTW and Accelerate backends; a thin VST3 adapter; embedded
presets; real-library, numerical, lifecycle and package-consumer tests. The
[VST3 guide](../vst3.md) records the current contract and release limitations.

The adapter handles mono/stereo, float/double host buffers and persistent stream
history. Internal DSP is float; reported latency is 256 samples. A preset must
match the host sample rate and layout. There is no automatic IR resampling,
multichannel VST3 bus support, custom editor, signed installer or release pipeline.
SDK validation is not evidence of compatibility with every host.

## Release scope

1. **Buildable VST3 beta:** a reproducible source recipe, relocatable binary
   archives, preset converter, redistributable examples and complete instructions.
2. **Production VST3 downloads:** verified dependencies, signed installation
   packages, macOS notarization, clean-machine installation and named-host evidence.
3. **Audio Unit follow-on:** reuse the same DSP/state model, initially AUv2 for
   desktop Mac hosts. Do not make AU implementation a prerequisite for VST3 release.

Keep CLAP and LV2 as separate demand-led adapters. Consider AAX only with a clear
Pro Tools audience and a separate SDK/signing/licence assessment. A standalone
system-audio application, virtual device/driver, custom editor, multichannel
expansion and automatic resampling are separate projects.

## Proposed compatibility matrix

| Deliverable | Initial candidate | Dependency policy |
| --- | --- | --- |
| Windows VST3 + preset tool | Windows 11, x86-64 | Static FFTW; documented MSVC runtime handling; include other required non-system libraries |
| macOS VST3 + preset tool | macOS 14+, arm64 and x86-64, universal package when both pass | Accelerate for the plug-in; bundle the preset tool's libsndfile dependencies |
| Linux VST3 + preset tool | Ubuntu 22.04+ compatible x86-64 baseline | Build against the oldest declared libc baseline; audit/bundle allowed non-system dependencies |
| Source builds | C++20, CMake 3.25+ for VST3; core retains 3.20+ | Document FFTW and Accelerate alternatives; dependencies acquired explicitly |

These are proposed compatibility floors, not statements about vendor OS support
or verified hardware. Before publishing a supported floor, run installation and
host tests there. If a candidate cannot be tested, publish a narrower tested
matrix rather than implying compatibility from a deployment-target flag.
Windows ARM64, 32-bit hosts and other Linux architectures are not initial binary
promises. Builds must not require the release machine's optional CPU instructions.

## Product and architecture requirements

- Keep C++20 and the independent `Convolver::core`, `Convolver::host` and
  `Convolver::io` boundaries. No new GUI framework or plug-in SDK enters the core.
- Keep CMake 3.20 for core consumers and 3.25 for VST3 builds.
- Use FFTW on Windows/Linux and Accelerate for the first macOS downloads; retain
  tested FFTW source-build support on macOS.
- Keep file access, decoding, planning and filter preparation off the audio callback.
- Preserve processor identifiers and version-1 state compatibility. Package format
  changes must not create a new plug-in identity or break saved projects.
- Ship the preset converter and rate/layout-labelled examples. Presets embed their
  IRs and must survive moving a project to a machine without the original files.
- Report invalid presets and rate/layout mismatches clearly; preserve the last
  valid state. The documentation must describe actual adapter behavior.
- Use portable release numbering, separate from historical 4.3/4.4/4.5. Propose
  `0.3.0-beta.1`, then `0.3.0` after the release gates; coordinate with any
  interoperability work landing in the same minor release.
- Preserve the historical code and website byte-for-byte. Put current instructions
  in maintained Markdown. Identify the project author as `jrp`.
- Provide licence notices, dependency versions, corresponding source and build
  scripts for every downloadable release. Do not rely on a moving dependency URL.

## User experience

The first guide has two paths: **download and install** and **build from source**.
Both end at the same smoke test: load an identity preset, pass audio unchanged
apart from the documented latency, then load a provided non-identity example.

The initial filter workflow remains: choose the correct rate/layout, run the
bundled converter on a WAV or supported configuration, load the resulting preset
in the host, and save the project. Include a 44.1/48/96 kHz example set and
instructions for other rates. Explain where to check input/output levels and
avoid accidental clipping. Do not silently normalize or resample user filters.

A plug-in requires a compatible host. Do not imply that installing it inserts
Convolver into every media player or the whole operating system. Document
verified routing recipes separately from format compatibility.

## Definition of a production release

All advertised OS/architecture combinations have matching source and downloadable
artifacts, successful automated tests, clean-machine installation evidence and
named-host smoke results. Signed/notarized packages are tested after download,
not only before archiving. A user can install, load, configure, save/reopen,
upgrade and uninstall using the documentation without developer tools or source
assets. Unknown host/platform combinations remain explicitly unverified.

Release signing identities, a place to store CI secrets, access to minimum-OS
test machines, and selected host licences/testers are execution dependencies.
Their absence may limit signed production distribution; it does not prevent
source builds or honestly labelled unsigned developer previews.

## Format and distribution evidence

Checked 18 October 2026; revisit changing SDK and signing requirements at release:

- [Steinberg VST SDK and MIT licence](https://www.steinberg.net/developers/vstsdk/).
- [Ableton's supported formats](https://help.ableton.com/hc/en-us/articles/5937501570460-Supported-Plug-in-Formats).
- [Apple Audio Units support](https://support.apple.com/en-gb/102082).
- [SoundSource Audio Unit hosting](https://rogueamoeba.com/support/manuals/soundsource/?page=audio-unit-effects).
- [Apple Developer ID distribution](https://developer.apple.com/developer-id/).
- [CLAP ABI and licence](https://github.com/free-audio/clap).
- [LV2 interface and host ecosystem](https://lv2plug.in/).
- [Avid AAX SDK requirements](https://developer.avid.com/aax/).
- [GPL source-distribution guidance](https://www.gnu.org/licenses/gpl-faq.en.html#UnchangedJustBinary)
  and [FFTW licensing](https://www.fftw.org/faq/section1.html#isfftwfree).
