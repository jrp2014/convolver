<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver plug-ins

Portable Convolver applies FIR filters to a continuous audio stream.
The current VST3 build is a developer preview. Production downloads, minimum-OS
qualification and named-host certification are not yet available.

- **Build:** follow [the pinned source recipe](build.md).
- **Install a preview artifact:** follow [installation](install.md), then
  [the first-use walkthrough](quickstart.md).
- **Compatibility:** read [host evidence and test procedure](hosts.md).
- **Problems:** see [troubleshooting](troubleshooting.md).
- **Tools:** see [the tools in `bin/`](#tools-in-bin).

The preview contains the VST3 bundle, two command-line tools, 12 synthetic
example presets, this guide and dependency licence notices. Every candidate must have
matching source and checksums. Verify those before installing. Preview CI artifacts
are temporary build outputs; they are not signed public releases.

The plug-in has one input and one output bus of 1 to 8 channels each; the counts
may differ (for example 2 in, 6 out for a crossover). The example presets cover
mono and stereo. Previews are **unsigned** on macOS and Windows for now; see
[installation](install.md) for what that means.

Convolver needs a VST3 host. Installing it alone does not process every player or
all system audio. Historical WMP, DirectShow and VST2 binaries belong to the old
Windows implementation; old plug-in identities and projects do not automatically
migrate. LV2 and Audio Unit (AUv2) adapters are planned alongside VST3 (see the
plug-in formats plan in the source repository's `docs/plans/`); CLAP,
a custom editor, AAX and a system-audio application are not part of this preview.

## Tools in bin

Run these from the extracted package directory; on Windows add `.exe`
(`./bin/convolver-preset.exe` in PowerShell). Outputs must be new paths: the
tools never replace an existing file or folder. Both print `--help`.

### convolver-preset

Makes a preset from an impulse response (IR), a `.cfg` or a configuration
list. This preview loads only `.vstpreset`; the tool also writes `.aupreset`
and LV2 preset folders for the Audio Unit and LV2 plug-ins that are still being
written. See [plug-in presets](../presets.md).

<!-- executable-example -->

```sh
./bin/convolver-preset presets/echo-48000-stereo/filter.cfg echo.vstpreset
```

`convolver-vst3-preset`, the older name of this tool in source builds, is not
in the package; `convolver-preset` takes the same arguments.

### convolver-equalizer-apo

Exports the same inputs for
[Equalizer APO](https://sourceforge.net/projects/equalizerapo/), which filters
everything a Windows device plays, with no plug-in host. It runs on every
platform, so you can make the export on a Mac and copy the folder to Windows.
A device that no exported filter fits is silenced. The export has not yet been
tried in Equalizer APO itself. See [Equalizer APO export](../equalizer-apo.md)
for installing the folder and choosing the device's sample rate.

<!-- executable-example -->

```sh
./bin/convolver-equalizer-apo presets/echo-48000-stereo/filter.cfg room
```
