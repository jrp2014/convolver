<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Check a stereo filter in a host

For anyone who has downloaded a preview package and wants to see, from start to
finish, that the Convolver plug-in filters exactly as it should in their host.
You will install the package, render the stereo example through the plug-in,
check the result with a command, save and reopen the project, and see what
happens when a preset does not fit.

**In short:** check the package's checksum, install the VST3 plug-in, load
`examples/first-use/stereo/filter.vstpreset` on a stereo track at 48000 Hz,
render `input.wav` through it, and run
`./bin/convolver --compare host-render.wav examples/first-use/stereo/expected.wav`.
It should print `Match`. The filter must survive saving and reopening, and a
preset for another rate or channel count must leave it playing.

**Status:** Convolver is a developer preview, and these steps have not yet
been followed in a host from start to finish. Menu names and what a host
shows may differ from what is written here; the
[status page](../project/status.md) says what is ready.

The steps name REAPER where a host's menus matter; other VST3 hosts have the
same commands under their own names. Commands are for macOS and Linux, run in
Terminal from the extracted package folder; on Windows, use PowerShell and
`.\bin\convolver.exe`.

## 1. Check and extract the package

Download the package for your system and processor, and the SHA-256 checksum
published with it. Then compare them:

```sh
shasum -a 256 Convolver-VERSION-preview-macos-arm64.zip
```

On Linux use `sha256sum`, and on Windows `Get-FileHash`. The 64 characters
printed must equal the published checksum; if they do not, download again.

Extract the archive into a new folder and open Terminal there. On macOS a
browser marks the download as quarantined, so clear that for the tools only:

```sh
xattr -dr com.apple.quarantine bin
./bin/convolver --version
```

The last command prints `Convolver portable` and the package's version.

## 2. Install the plug-in

Close the host, then copy the whole plug-in bundle into your plug-in folder,
removing any older copy first so no old files stay inside the bundle. On
macOS:

```sh
mkdir -p ~/Library/Audio/Plug-Ins/VST3
rm -rf ~/Library/Audio/Plug-Ins/VST3/Convolver.vst3
cp -R VST3/Convolver.vst3 ~/Library/Audio/Plug-Ins/VST3/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Convolver.vst3
```

[Install](install.md#where-each-plug-in-goes) gives the folders for Windows and
Linux. Open the host and rescan its plug-ins (REAPER: *Settings › Plug-ins ›
VST*, **Re-scan**). Convolver is listed among the VST3 effects.

## 3. See the expected result with the command-line tool

Before using the host, render the example with `convolver` itself and compare
it with the expected output:

<!-- executable-example -->
```sh
./bin/convolver --filter examples/first-use/stereo/filter.cfg --input examples/first-use/stereo/input.wav --output cli-render.wav
```

<!-- executable-example -->
```sh
./bin/convolver --compare cli-render.wav examples/first-use/stereo/expected.wav
```

The second command ends with `Match: no sample differs by more than the limit
(0.0001 of the expected peak, -80 dB, or two 16-bit steps).` The example filters each side differently,
feeds some of the left into the right and delays the right by 1 ms, so a
swapped, missing or misrouted channel would not match.
`examples/first-use/stereo/` holds the filter (`filter.cfg` and `room.wav`),
`input.wav`, `expected.wav` and `filter.vstpreset`, a preset made from the
filter.

## 4. Render the example in the host

1. Set the project and the audio device to 48000 Hz (REAPER: *File › Project
   settings*). The example is made for 48000 Hz.
2. On a new stereo track, insert `examples/first-use/stereo/input.wav` at the
   very start of the project. Make sure the clip has no fade in or out; some
   hosts add a short one to new clips (REAPER: select it, *Item properties*,
   set both fades to 0).
3. Add Convolver to the track as an effect. It has no window of its own: the
   host shows its generic controls, with a read-only **Status**.
4. Load `examples/first-use/stereo/filter.vstpreset` from the host's preset
   menu (REAPER: the **+** button beside the preset menu, then the item that
   imports a VST preset file). Status reads
   `Filter loaded: 48000 Hz, 2 in, 2 out.`
5. Leave the track's volume, pan and the master at their defaults, and render
   the whole project offline to `host-render.wav` in the package folder:
   48000 Hz, stereo, 32-bit float WAV, no normalising or dither, with a tail of
   at least 100 ms after the end so the filter can ring out (REAPER: *File ›
   Render*, *Bounds: Entire project*, tick *Tail*).
6. Compare the render with the expected output:

   ```sh
   ./bin/convolver --compare host-render.wav examples/first-use/stereo/expected.wav
   ```

   It prints `Match`. If it prints `Different`, the line after it says why:
   output that is late means the host did not make up for the plug-in's
   256-frame delay (turn on its delay compensation); output that ends too soon
   needs a longer tail; another level means a volume, pan or gain setting
   changed it. A 16- or 24-bit render also matches.

## 5. Save and reopen

Save the project, quit the host, open the project again and render a second
time to `host-render-reopened.wav`. Comparing it with `expected.wav` again
prints `Match`: the project kept the filter. The preset holds the whole filter,
so the example's WAV and `.cfg` files are not needed once it is loaded.

## 6. Load a preset that does not fit

1. Load `presets/echo-44100-stereo/filter.vstpreset`, made for 44100 Hz. The
   stereo filter keeps playing, and Status reads `Load failed, previous filter
   kept: the filter is for 44100 Hz, the host runs at 48000 Hz. Details in the
   Convolver log.` Some hosts also report the failure themselves.
2. Load `presets/echo-48000-mono/filter.vstpreset`, made for one channel.
   The stereo filter keeps playing, and Status says the filter has
   `1 in, 1 out`, the plug-in `2 in, 2 out`.
3. Render again and compare: still `Match`. To recover, load the stereo
   preset from step 4 again, or a preset made for your host's rate and the
   track's channels.

## If something went wrong

- The host does not list Convolver, or the plug-in plays silence:
  [troubleshooting](troubleshooting.md).
- `--compare` refuses a file at another rate or channel count: render at
  48000 Hz in stereo. Its options are in the
  [command-line reference](command-line.md#comparing-two-files).
- To make a preset from your own filter, see
  [your first sound in a host](first-sound-in-a-host.md#make-a-preset-from-your-own-filter).
