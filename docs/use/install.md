<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Install the plug-ins

For anyone putting a Convolver plug-in where a host can find it, from a preview
package or from your own build. Afterwards, follow
[your first sound in a host](first-sound-in-a-host.md).

**In short:**

- Copy the whole plug-in bundle (a folder, not just the file inside it) into a
  folder your host scans, then ask the host to rescan.
- Preview packages contain the VST3 plug-in for Windows, macOS and Linux. The
  Audio Unit (macOS) and LV2 (Linux) plug-ins are built from source for now.
- Previews are not signed. On macOS, clear the download quarantine for the
  Convolver bundle only; on Windows, an installer shows a SmartScreen warning.

## What a preview package contains

`VST3/Convolver.vst3`, the tools in `bin/`, example presets in `presets/`,
these pages in `docs/` and licence notices in `licenses/`. Check the package's
SHA-256 checksum against the one published with it before going further.

Windows needs the Microsoft Visual C++ 2015–2022 x64 runtime. The plug-in has
its own FFT code and needs no other library.

## Where each plug-in goes

Close the host first. Copy the complete bundle; keep the tools, examples and
these pages together in a separate folder.

| Plug-in | System | Folder for your account | For all accounts |
| --- | --- | --- | --- |
| VST3 (`Convolver.vst3`) | Windows | `%LOCALAPPDATA%\Programs\Common\VST3\` | `%COMMONPROGRAMFILES%\VST3\` (administrator) |
| VST3 | macOS | `~/Library/Audio/Plug-Ins/VST3/` | `/Library/Audio/Plug-Ins/VST3/` |
| VST3 | Linux | `~/.vst3/` | `/usr/lib/vst3/` |
| Audio Unit (`Convolver.component`) | macOS | `~/Library/Audio/Plug-Ins/Components/` | `/Library/Audio/Plug-Ins/Components/` |
| LV2 (`convolver.lv2`) | Linux | `~/.lv2/` | `/usr/lib/lv2/` |

Then reopen the host, turn on scanning for that format if needed, and use its
rescan command. Exact menu names depend on the host. Steinberg lists every
standard [VST3 location](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BLocations.html).

**Audio Unit:** macOS caches its list of units. After copying, refresh it and
check the unit with Apple's validator:

```sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aufx Conv CONV
```

The unit appears as **Convolver: Convolver** among effects. Logic and
GarageBand run the same check when they first see it.

**LV2:** instead of copying, you can add the build folder to the host's search
path, for example by starting the host with
`LV2_PATH="$HOME/src/convolver/build/lv2:$HOME/.lv2:/usr/local/lib/lv2:/usr/lib/lv2"`.
`lv2ls` lists what the host will see; Convolver appears once per channel
layout ([LV2 layouts](plugins.md#lv2-channel-layouts)).

## Unsigned previews

Previews are not yet signed with an Apple Developer ID or a Windows
code-signing certificate.

- **macOS:** the bundle carries only an ad-hoc signature. Files downloaded with
  a browser are quarantined, and hosts may refuse to load a quarantined
  unsigned bundle. After copying, clear the quarantine for this bundle only:

  ```sh
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Convolver.vst3
  ```

  Do the same for the tools if you use them (`xattr -dr com.apple.quarantine bin`
  in the package folder). This changes nothing else; do not turn off Gatekeeper
  for the whole system.
- **Windows:** an unsigned installer shows "Windows protected your PC". Choose
  **More info**, check that the publisher is "Unknown publisher" and that the
  file name matches, then **Run anyway**. Copying the bundle from the ZIP
  shows no warning.

## Installers

Preview installers are also made for Windows and macOS; they have not yet been
tried on clean computers. The Windows installer puts the plug-in in the
all-users VST3 folder and the tools in the Convolver folder you choose;
upgrading removes the previous version's files first. The macOS package
installs into `/Library/Audio/Plug-Ins/VST3` and
`/Library/Application Support/Convolver`, which holds `uninstall.sh`. Run that
script with administrator rights only to remove Convolver.

## Replace or remove

Close the host before replacing or removing a bundle, and keep backups of your
projects. To remove a copied installation, delete only the bundle you copied
and the extracted package folder. Uninstallers leave other plug-ins, projects
and your own presets alone; on Windows, a file in use stops the uninstaller,
which you can then run again.

## Check it worked

The host lists Convolver among its effects. If it does not, read the host's
scan log and see [troubleshooting](troubleshooting.md#the-host-does-not-list-convolver).

To build the plug-ins yourself, see [build from source](../develop/build.md#plug-ins).
