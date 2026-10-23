<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Audio Unit adapter

[Documentation index](README.md) · [Audio concepts](concepts.md) · [Plug-in presets](presets.md)

**In short:**

- `Convolver.component` is an Audio Unit (AUv2) effect for macOS hosts such as
  Logic, GarageBand and SoundSource. Build it with `-DCONVOLVER_BUILD_AU=ON` and
  the pinned AudioUnitSDK, then copy it to `~/Library/Audio/Plug-Ins/Components/`.
- There is no editor. Turn a WAV, `.cfg` or configuration list into a
  `.aupreset` with `convolver-preset`, then load it from the host's preset
  menu. The preset embeds the filter, so the original files are not needed later.
- One input bus and one output bus of 1 to 8 channels each; the counts may
  differ (for example 2 in, 6 out for a crossover). The preset must match the
  host's sample rate and channel counts.
- Latency is 256 samples, which the unit reports to the host in seconds. Bypass
  fades over 64 samples to the input delayed by that latency.
- The component is distributed under **GPL-3.0-or-later**, because it includes
  Apple's AudioUnitSDK (Apache-2.0); Convolver's sources and other builds stay
  GPL-2.0-or-later.
- These are developer builds: ad-hoc signed only (not Developer ID signed or
  notarized), and not yet tested in named hosts (TODO AU-4).

Contents: [build](#build) · [install](#install) · [loading filters](#loading-filters) ·
[behaviour](#behaviour) · [licence](#licence) · [testing](#testing)

## Build

The adapter needs macOS 11 or later, CMake 3.20+, Xcode's command-line tools
(C++23, for AudioUnitSDK 1.4.0) and the AudioUnitSDK commit pinned in
`packaging/release-policy.json`
([pinned format dependencies](plugins/build.md#pinned-format-dependencies)).
Fetch and unpack it, then configure with it:

```sh
python3 scripts/release/sources.py --fetch --cache build/downloads \
  --destination build/sources audiounitsdk
cmake -S . -B build-au -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_BUILD_AU=ON \
  -DCONVOLVER_AU_SDK="$PWD/build/sources/AudioUnitSDK"
cmake --build build-au --parallel
ctest --test-dir build-au --output-on-failure
```

The component is `build-au/plugins/au/Convolver.component`. As for VST3, the
default FFT backend (`AUTO`) of a plug-in build is Apple's Accelerate, so the
component links no FFT library; libsndfile is used only by the command-line
tools and tests. Do not combine `CONVOLVER_SANITIZERS=ON` with a component you
intend to load in a host: hosts and `auval` cannot load sanitizer-instrumented
code.

## Install

Copy the whole bundle into your user's component folder, then let macOS notice
it:

```sh
ditto build-au/plugins/au/Convolver.component \
  ~/Library/Audio/Plug-Ins/Components/Convolver.component
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aufx Conv CONV
```

`auval` is Apple's validator; hosts such as Logic run it themselves when they
first see the component. The unit appears as **Convolver: Convolver** under
effects. Its codes `aufx`, `Conv` and `CONV` are permanent
([plug-in identities](plugin-identities.md)).

## Loading filters

Make a preset with `convolver-preset` and load it from the host's preset menu
("Load Preset…" or the preset browser):

```sh
convolver-preset room.cfg room.aupreset
```

The `.aupreset` is the dictionary Audio Units save as `ClassInfo`, with the
encoded filter under the key `convolver-state`
([what the files contain](presets.md#what-the-files-contain)). A host saves the
same dictionary in its project, so saving and reopening keeps the filter even
after the original WAV and `.cfg` files are gone. Logic and GarageBand list
presets saved under `~/Library/Audio/Presets/Convolver/Convolver/`.

A preset that does not fit the host is handled as for every Convolver plug-in
([when a preset does not fit](presets.md#when-a-preset-does-not-fit)):

- The **first** filter of an instance that cannot be used (wrong sample rate or
  channel counts, damaged data, another plug-in's preset) makes the unit play
  **silence**, never the unfiltered input. The host gets an error, and the
  explanation goes to the system log: open Console and search for "Convolver".
- A filter that is rejected while another is playing leaves the playing filter
  unchanged.
- A project restored before the host has set its sample rate keeps its filter:
  if the rate turns out to be wrong the unit plays silence, and correcting the
  rate brings the filter back. The rate and channels count as set once the
  unit has processed audio with them; some hosts (those built on JUCE)
  initialize the unit briefly at a default 44.1 kHz just to try a channel
  layout, and that alone does not count.

## Behaviour

- **Channels.** The unit offers every pairing of 1 to 8 input and output
  channels. With different counts, outputs are fed only by the filter's routes:
  an empty instance (no filter yet) plays silence, as does bypass, so a
  crossover never sends full-range audio to a tweeter. With equal counts an
  empty instance passes input *c* to output *c*.
- **Latency and tail.** Reported in seconds, as Audio Units do: 256 samples
  divided by the sample rate, and the filter's tail (its longest response plus
  delays) likewise. After the filter is replaced during playback the tail
  stays at the longest filter used until the host reinitializes the unit. The
  host is notified when either value changes.
- **Bypass.** The host's bypass switch (`kAudioUnitProperty_BypassEffect`) ramps
  over 64 samples to the input delayed by 256 samples, and back; the filter
  keeps running underneath. It is not Apple's `AUEffectBase` pass-through, which
  would skip the delay and the ramp. Bypass is saved with the filter.
- **Replacing a filter.** Loading a preset while audio plays prepares the new
  filter off the audio thread, then crossfades to it over 256 samples, as in
  the [VST3 lifecycle](vst3.md#processing-and-lifecycle).
- **Silence.** The unit marks its output silent only once the output really is
  silent, after the filter's tail has decayed.
- **Formats.** 32-bit float, non-interleaved, at a whole-number sample rate that
  is the same on input and output. Convolver does not resample.

## Licence

The built component combines Convolver (GPL-2.0-or-later) with AudioUnitSDK
(Apache-2.0). Apache-2.0 is compatible with version 3 of the GPL but not
version 2, so the component as a whole is distributed under GPL-3.0-or-later
(owner's decision, 21 October 2026; see "AU licence" in the
[plug-in formats plan](plans/2026-10-21-plugin-formats.md#au-licence)). The
bundle carries `Contents/Resources/licenses/` with the notice, the GPL version 3
and AudioUnitSDK's licence.

## Testing

Three CTest tests run when the adapter is built (label `au`):

- `au.harness`: the shared [PLUG-2 comparisons](testing.md#plug-in-comparisons-plug-2)
  through the Audio Unit API, with filters loaded as `.aupreset` dictionaries
  and latency and tail converted from seconds.
- `au.adapter`: what is specific to Audio Units: the channel pairs offered,
  rejected formats, latency and tail in seconds with change notifications, the
  `ClassInfo` keys, foreign, damaged, newer and failed presets with the
  failure record each leaves (read through a custom property, 64800, that
  exists for the tests only), an initialization that cannot build its
  processor, bypass switched from
  another thread while rendering, the output silence flag, and no C++
  allocation while rendering.
- `au.bundle`: the bundle's codes and version against the
  [identity registry](plugin-identities.md), a single exported symbol
  (`ConvolverAUFactory`), the licence files and the signature.

The tests register the unit inside the test process with
`AudioComponentRegister`, so they need no installation. The
[Audio Unit workflow](../.github/workflows/au.yml) runs them on macOS, then
installs the component for the runner's user and runs `auval -strict`.
Host results (Logic, GarageBand, SoundSource, Live, REAPER and FL Studio) are
TODO AU-4.
