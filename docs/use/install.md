<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Install the plug-ins

For anyone putting a Convolver plug-in where a host can find it, from a preview
package or from your own build. Afterwards, follow
[your first sound in a host](first-sound-in-a-host.md).

**In short:**

- Copy the whole plug-in bundle (a folder, not just the file inside it) into a
  folder your host scans, then ask the host to rescan.
- Preview packages contain the VST® 3 plug-in on macOS, Windows and Linux,
  the Audio Unit on macOS and the LV2 plug-in on Linux.
- Previews are not signed. On macOS, allow the installer once in
  **Privacy & Security**, or, from the ZIP archive, clear the download
  quarantine for the Convolver bundles only; on Windows, an installer shows a
  SmartScreen warning.

## What a preview package contains

`VST3/Convolver.vst3`, on macOS the Audio Unit `Components/Convolver.component`,
on Linux the LV2 bundle `LV2/convolver.lv2`, the tools in `bin/` (`convolver`, `convolver-preset`
and `convolver-equalizer-apo`), example filters and their presets in `presets/`, the first-use
examples with presets made from them in `examples/first-use/`, these pages in
`docs/use/` (start with `docs/use/README.md`) and licence notices in `licenses/`. Check the package's
SHA-256 checksum against the one published with it before going further.

The Audio Unit is licensed as a whole under GPL-3.0-or-later, because it is
built with Apple's AudioUnitSDK (Apache-2.0); everything else is
GPL-2.0-or-later ([Audio Unit licence](plugins.md#audio-unit-licence)).

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

**LV2:** from the unpacked Linux package, copy the bundle for your account:

```sh
mkdir -p ~/.lv2
cp -R LV2/convolver.lv2 ~/.lv2/
```

Replace any older `convolver.lv2` there rather than keeping both. With your
own build, you can instead add the build folder to the host's search path,
for example by starting the host with
`LV2_PATH="$HOME/src/convolver/build/lv2:$HOME/.lv2:/usr/local/lib/lv2:/usr/lib/lv2"`.
`lv2ls` (in Debian's and Ubuntu's `lilv-utils`) lists what the host will
see; Convolver appears once per channel layout
([LV2 layouts](plugins.md#lv2-channel-layouts)).

## Unsigned previews

Previews are not yet signed with an Apple Developer ID or a Windows
code-signing certificate.

- **macOS installer (`.pkg`):** check the file's SHA-256 against the release
  notes first. The first time you open it, macOS says it cannot verify the
  installer or its developer; close that message (**Done** or **OK**). Open
  **System Settings**, then **Privacy & Security**, scroll down to the message
  that the installer was blocked and choose **Open Anyway**. macOS asks once
  more (**Open** or **Open Anyway**) and then for your password or Touch ID,
  and the installer starts. **Open Anyway** is offered for about an hour after
  the first attempt.
- **macOS ZIP archive:** the bundle carries only an ad-hoc signature. Files
  downloaded with a browser are quarantined, and hosts may refuse to load a
  quarantined unsigned bundle. After copying, clear the quarantine for this
  bundle only:

  ```sh
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Convolver.vst3
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Convolver.component
  ```

  Do the same for the tools if you use them (`xattr -dr com.apple.quarantine bin`
  in the package folder). This changes nothing else; do not turn off Gatekeeper
  for the whole system.
- **Windows:** an unsigned installer shows "Windows protected your PC". Choose
  **More info**, check that the publisher is "Unknown publisher" and that the
  file name matches, then **Run anyway**. Copying the bundle from the ZIP
  shows no warning.

## Installers

Preview installers are also made for macOS and Windows; Linux has the ZIP
archive only. They have not yet been tried by a person on a clean computer. The Windows installer puts the plug-in in the
all-users VST3 folder and the tools in the Convolver folder you choose;
upgrading removes the previous version's files first. The macOS package
installs into `/Library/Audio/Plug-Ins/VST3`,
`/Library/Audio/Plug-Ins/Components` (the Audio Unit) and
`/Library/Application Support/Convolver`, which holds `uninstall.sh`; its
**Customize** button lets you leave out the VST3 plug-in, the Audio Unit or the
tools. Run `uninstall.sh` with administrator rights only to remove Convolver.
After installing, refresh the Audio Unit list as above.

## Replace or remove

Close the host before replacing or removing a bundle, and keep backups of your
projects. To replace a copied bundle, delete the old one first and then copy
the new one, so no files from the old version stay inside it. To remove a copied installation, delete only the bundle you copied
and the extracted package folder. Uninstallers leave other plug-ins, projects
and your own presets alone; on Windows, a file in use stops the uninstaller,
which you can then run again.

## Check it worked

The host lists Convolver among its effects. If it does not, read the host's
scan log and see [troubleshooting](troubleshooting.md#the-host-does-not-list-convolver).

To build the plug-ins yourself, see [build from source](../develop/build.md#plug-ins).

<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->

<img src="images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible logo" width="120">

Convolver supports VST® 3. VST is a registered trademark of Steinberg Media Technologies GmbH.

<!-- End of the VST trademark notice -->
