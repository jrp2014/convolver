# Convolver plug-ins

Portable Convolver **0.2.0** applies FIR filters to a continuous audio stream.
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

Convolver needs a VST3 host. Installing it alone does not process every player or
all system audio. Historical WMP, DirectShow and VST2 binaries belong to the old
Windows implementation; old plug-in identities and projects do not automatically
migrate. AUv2, an editor, CLAP/LV2, AAX and a system-audio application remain
separate projects.
