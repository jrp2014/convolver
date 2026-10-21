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

The preview contains the VST3 bundle, preset converter, 12 synthetic example
presets, this guide and dependency licence notices. Every candidate must have
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
