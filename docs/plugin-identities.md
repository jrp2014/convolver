<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in identities and channel layouts

[Documentation index](README.md) · [VST3 guide](vst3.md) ·
[Plug-in formats plan](plans/2026-10-21-plugin-formats.md)

**In short:** every plug-in format identifies Convolver by fixed codes: VST3
class IDs, AU type, subtype and manufacturer codes, LV2 URIs, and macOS bundle
and installer identifiers. Hosts save these codes in projects and presets, so
once a build using them is released they **never change**. They are recorded
once, in [`plugins/identities.json`](../plugins/identities.json), together with
the channel layouts the plug-ins support; the build and the release scripts
read them from there. **If you distribute a modified build, please change every
identity in that file** so hosts do not confuse your plug-in with this one.
Presets saved today (state version 1) record no channel layout and keep
loading on any host arrangement with the same channel counts.

Contents: [the identities](#the-identities) · [forks](#if-you-distribute-a-modified-build) ·
[channel layouts](#channel-layouts) · [old presets](#presets-without-a-layout) ·
[how the build uses the registry](#how-the-build-uses-the-registry) ·
[changing the registry](#changing-the-registry)

## The identities

| Format | Identity | Value | Notes |
| --- | --- | --- | --- |
| VST3 | Processor class ID | `73A81329E6484ADF9DA7D731A8C94201` | Hosts and `.vstpreset` files refer to the plug-in by this |
| VST3 | Controller class ID | `622AD87C23664CCFB157473FBCD4A6B9` | |
| VST3 | macOS bundle identifier | `io.github.jrp2014.convolver.vst3` | `CFBundleIdentifier` in `Convolver.vst3` |
| AU | Type, subtype, manufacturer | `aufx`, `Conv`, `CONV` | Owner's decision, 21 October 2026 |
| AU | macOS bundle identifier | `io.github.jrp2014.convolver.au` | For `Convolver.component` (AU-1) |
| LV2 | URI base | `https://github.com/jrp2014/convolver/lv2#` | Plug-in URIs add a [fixed layout](#channel-layouts)'s id; state keys and the filter parameter use the same base |
| LV2 | Bundle | `convolver.lv2` | |
| macOS installer | Package identifiers | `io.github.jrp2014.convolver.pkg.vst3`, `….pkg.tools` | Installer receipts (`pkgutil --pkgs`) |
| Windows installer | Registry keys | `HKLM\Software\Convolver`, uninstall key `Convolver` | Used to find and remove a previous installation |
| All | Host state | magic `CNVS`, version 1 | The encoded filter inside presets and saved projects |
| Historical | VST2 unique ID | `Conv` | The 2005–2006 Windows plug-in; VST3-6 may map old projects to VST3 |

**Status (22 October 2026):** no release has used these identities yet. The
AU codes are the owner's decision of 21 October 2026. The LV2 URI scheme, the
eight fixed layouts and the move of the macOS identifiers from `org.convolver.*`
(a domain the project does not control) to `io.github.jrp2014.convolver.*` are
proposed in [PLUG-4](../TODO.md#plug-ins-vst3-lv2-and-au) and await jrp's
decision. The VST3 class IDs are the ones the adapter has always used.

About the AU codes: hosts identify an Audio Unit by type, subtype and
manufacturer together. Apple no longer runs a registration service for the
manufacturer code; it must contain at least one uppercase letter (all-lowercase
codes are Apple's) and must not be `DEMO`. No other public use of `CONV` was
found, which cannot prove there is none. The subtype repeats the historical VST2
ID.

## If you distribute a modified build

Please change **every** value in `plugins/identities.json` before you
distribute anything. This is a request to avoid confusing hosts and users, not
a condition of the licence: generate two new VST3 class IDs (any 128-bit random values, written as
32 uppercase hex digits), choose your own AU manufacturer code, use an LV2 URI
base and macOS identifiers under a domain or account you control, and rename the
Windows registry keys. `packaging/macos/Distribution.xml` and
`packaging/windows/installer.nsi` repeat the installer identifiers, so edit them
to match; the tests check that they agree. Then update the frozen copy in
[`tests/identities_tests.py`](../tests/identities_tests.py). Keep the host-state
magic only if your state format stays identical.

Otherwise a host that has both builds installed sees one plug-in, loads the
wrong one for a project, or lets one installer remove the other.

## Channel layouts

Each side of a filter, input and output independently, has one of two
meanings:

- **Discrete:** channels are numbered 1 to N in the host's bus order. What they
  carry is up to the filter's author: crossover bands, microphones, or speakers
  in an order the host does not know about. Any host arrangement with N channels
  is accepted.
- **Named:** channels are speaker positions, given as a WAV channel mask (the
  `WAVE_FORMAT_EXTENSIBLE` bits that the CLI's `--speaker-map` writes), in mask
  bit order: for example front left, front right, centre, LFE, back left, back
  right for mask `0x3F`. An adapter checks the host's arrangement against it, or
  maps it where the format defines a mapping.

The registry's `speakers` list names each mask bit for every format. Bits 0 to 17
of a WAV mask, a VST3 `SpeakerArrangement` and an AU `AudioChannelBitmap` have
the same meaning: bit 4 is WAV `BL`, VST3 `kSpeakerLs` and AU
`kAudioChannelBit_LeftSurround`. VST3 arrangements also order their channels by
bit, so VST3 needs no translation. AU hosts usually describe a layout by a
layout tag or by channel labels instead, and their channel order need not be bit
order (`kAudioChannelLayoutTag_MPEG_5_1_C` is L C R Ls Rs LFE); some labels have
no bit at all. The AU adapter must therefore convert a tag or label list to a
mask and a channel map (for example with `kAudioFormatProperty_BitmapForLayoutTag`)
before comparing. Note too that 5.1 has two masks: `0x3F` with back speakers
(VST3 `k51`) and `0x60F` with side speakers; a strict check must decide whether
to treat them as the same layout. LV2 names
speakers with [port-group](https://lv2plug.in/ns/ext/port-groups) designations;
it has none for the height speakers (bits 11 to 17), so a layout using them can
only be discrete in LV2. The `vst3.identities` test checks the VST3 column
against the SDK headers.

**Channel counts.** VST3 (and AU, when it exists) accepts any input and output
count from 1 to 8, and the counts may differ (2 in, 6 out for a crossover).
LV2 plug-ins have fixed ports, so LV2 publishes one plug-in for each **fixed
layout** in the registry. Proposed (awaiting jrp): 1→1, 2→2, 1→2, 2→4, 2→6, 2→8,
6→6 and 8→8, all discrete, with ids such as `discrete-2-6`, so the 2→6 plug-in
is `https://github.com/jrp2014/convolver/lv2#discrete-2-6`. More are added only
on request. Layout names give counts and meaning ("2→6 discrete"), never a
speaker format such as "5.1", because six crossover outputs are not 5.1 speakers.

## Presets without a layout

The current host state (version 1) stores the input and output channel counts
and an *output* channel mask, and no input layout. Hence:

- A version-1 preset, or any filter with no recorded layout, is **unspecified**
  and treated as discrete on both sides. It loads on every host arrangement with
  matching counts, exactly as before, and channels are routed by bus index.
  Stricter layout checks added later must never reject it.
- Its output mask is advisory: an adapter may warn when it disagrees with the
  host's arrangement, but does not reject the preset.
- Recording an input layout, or making a layout binding, needs host-state
  version 2, which is a format decision for jrp. Decoders will keep reading
  version 1, and encoders keep writing version 1 whenever nothing new is
  recorded, so older builds still read those presets.

The VST3 adapter test `version1_presets` loads a 2→6 version-1 preset with
output masks 0, `0x3F` (5.1) and `0x60F` (5.1 with side speakers) on four
different six-channel arrangements, and checks every output by bus index.

## How the build uses the registry

- `cmake/ConvolverIdentities.cmake` reads the registry at configure time (a
  change re-runs CMake), rejects malformed codes, and writes
  `generated/convolver_identities.hpp` (namespace `convolver::identity`) from
  [`cmake/convolver_identities.hpp.in`](../cmake/convolver_identities.hpp.in).
  It also sets `CONVOLVER_ID_*` CMake variables such as
  `CONVOLVER_ID_VST3_BUNDLE`.
- The VST3 adapter takes its class IDs, vendor and channel bounds from the
  header, and `Info.plist.in` its bundle identifier from the variable.
- `scripts/release/candidate.py` reads the macOS package identifiers from the
  registry; `packaging/macos/Distribution.xml` and
  `packaging/windows/installer.nsi` must match it.
- LV2 and AU adapters will read the same header: the URI base and fixed layouts,
  the AU codes (`identity::four_char` packs them as an `OSType`) and the speaker
  designations.

## Changing the registry

[`tests/identities_tests.py`](../tests/identities_tests.py) runs in pre-commit,
lint CI and CTest (`identities`). It checks each format's rules, that the
registry still equals its frozen copy (every fixed layout included), that the
installer files agree with it, and that no other text file in the repository
keeps its own copy of a class ID, bundle identifier or LV2 URI base. AU codes
such as `Conv` are too common a word to search for, so reviews must catch
copies of those. So:

- **Adding** a fixed layout, a speaker or a new format's identity: edit the
  registry and the frozen copy in the test.
- **Changing or removing** a released identity or fixed layout: don't. A host
  would treat the result as a different plug-in, and saved projects would lose
  it. Before the first release, a change needs jrp's decision, recorded here
  and in the pull request.
