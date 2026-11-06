<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Check file rendering with the command line

For anyone who has downloaded a preview package and wants to see, from start to
finish, that the `convolver` command-line tool filters files exactly as it
should. You will check the package, filter the three example recordings, check
each result with a command, and see what Convolver says when a filter does not
fit the recording.

**In short:** check the package's checksum, then run
`./bin/convolver --filter F --input I --output O` on each folder in
`examples/first-use/` and `./bin/convolver --compare O expected.wav` on each
result: every comparison prints `Match`. A filter made for another sample rate
or channel count is refused with a message that says what differs, and no
output file is written.

**Status:** Convolver is a developer preview. These steps run automatically on
every package, but nobody outside the project has yet followed them from start
to finish; the [status page](../project/status.md) says what is ready.

Commands are for macOS and Linux, run in Terminal from the extracted package
folder. On Windows, use PowerShell and `.\bin\convolver.exe` in place of
`./bin/convolver`. To build the tool from source instead, see
[your first filter](first-filter.md).

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

The last command prints `Convolver portable` and the package's version, then
the FFT library and the libsndfile version it uses. Windows and Linux need no
quarantine step.

## 2. Filter the three examples

`examples/first-use/` holds three small filters, each with `input.wav` (85 ms
of white noise at half full scale, so turn the volume down before listening)
and `expected.wav`, the output it should give:

- `mono/impulse.wav`: one impulse response (IR) file used directly, with no
  configuration;
- `stereo/filter.cfg`: a configuration that filters each side differently,
  feeds some of the left into the right and delays the right by 1 ms;
- `crossover/filter.cfg`: a configuration that splits stereo into six
  outputs, low, mid and high bands for each side, with the high bands 6 dB
  lower and 0.5 ms later.

Filter each input:

<!-- executable-example -->
```sh
./bin/convolver --filter examples/first-use/mono/impulse.wav --input examples/first-use/mono/input.wav --output mono.wav
```

<!-- executable-example -->
```sh
./bin/convolver --filter examples/first-use/stereo/filter.cfg --input examples/first-use/stereo/input.wav --output stereo.wav
```

<!-- executable-example -->
```sh
./bin/convolver --filter examples/first-use/crossover/filter.cfg --input examples/first-use/crossover/input.wav --output crossover.wav
```

Each command writes a 32-bit float WAV in the package folder and prints
nothing unless something is wrong. Convolver removes its own processing delay
and keeps the whole filter tail, so each output lines up with its input and is
longer by the filter's length.

## 3. Check each output

Compare each output with the expected file:

<!-- executable-example -->
```sh
./bin/convolver --compare mono.wav examples/first-use/mono/expected.wav
```

<!-- executable-example -->
```sh
./bin/convolver --compare stereo.wav examples/first-use/stereo/expected.wav
```

<!-- executable-example -->
```sh
./bin/convolver --compare crossover.wav examples/first-use/crossover/expected.wav
```

Each prints four lines: the two files' rates, channels and lengths, which
must agree (`48000 Hz, 6 channels, 4249 frames` for the crossover), the
largest difference, and last `Match: no sample differs by more than the limit
(0.0001 of the expected peak, -80 dB, or two 16-bit steps).` The largest
difference is tiny, about -130 dB, from single-precision rounding. A swapped,
missing, misrouted or late channel would print `Different` and say why
([comparing two files](command-line.md#comparing-two-files)).

To see what the crossover does without filtering anything:

```sh
./bin/convolver --inspect examples/first-use/crossover/filter.cfg
```

It lists six paths, each taking input 0 or 1 to one output, with `x0.5` on
outputs 2 and 5 (the high bands), the output delays (`0.5 ms=24` frames on
those two), then `latency 256 frames, tail 153 frames`, and last `Valid`.

## 4. Try a filter that does not fit

A filter is made for one sample rate and fixed channel counts, and Convolver
never resamples or remixes to make it fit. The package's `presets/` folder
holds simple filters for other rates.

1. Filter the stereo input with a filter made for 44100 Hz:

   <!-- refused-example -->
   ```sh
   ./bin/convolver --filter presets/echo-44100-stereo/filter.cfg --input examples/first-use/stereo/input.wav --output wrong-rate.wav
   ```

   It is refused with this message, and `wrong-rate.wav` is not written:

   ```text
   convolver: error: presets/echo-44100-stereo/filter.cfg cannot be used with
   the audio to be filtered: its sample rate is 44100 Hz, not 48000 Hz
   ```

   The lines after it give the cause (the configuration's first line says
   44100 Hz), the fix (use a filter made for 48000 Hz, or convert the audio
   first) and a note that a configuration list can hold one configuration per
   rate and pick the matching one.

2. Filter the mono input with the stereo filter:

   <!-- refused-example -->
   ```sh
   ./bin/convolver --filter examples/first-use/stereo/filter.cfg --input examples/first-use/mono/input.wav --output wrong-channels.wav
   ```

   It is refused, and `wrong-channels.wav` is not written:

   ```text
   convolver: error: examples/first-use/stereo/filter.cfg cannot be used with
   the audio to be filtered: it takes 2 input channels, not 1
   ```

   The cause says the audio must have as many channels as the
   configuration's input count, and the fix suggests a configuration written
   for 1 input channel.

3. Recover with a filter made for the input's rate and channels:

   <!-- executable-example -->
   ```sh
   ./bin/convolver --filter presets/echo-48000-stereo/filter.cfg --input examples/first-use/stereo/input.wav --output echo.wav
   ```

   It writes `echo.wav`: the stereo input at half level with a quieter copy
   about 5 ms later, heard as a slightly hollow tone. Each refusal ends the
   command with exit status 1, so a script stops there too.

## 5. Filter your own file

Use your own IR or `.cfg` and a WAV file at the filter's rate, with 24-bit
output:

```sh
./bin/convolver --filter room.cfg --input music.wav --output corrected.wav --output-format wav-pcm24
```

There is no expected file to compare with, so listen to the result at a low
volume first. A filter with gain can push 24-bit output past full scale,
where it clips; `--gain-db no-clip` lowers the level so that it does not,
within the small margin that [gain](command-line.md#gain-and-attenuation)
gives. Every option is in the
[command-line reference](command-line.md).

## If something went wrong

- **macOS says the tool cannot be opened**, or that it is from an
  unidentified developer: run the `xattr` command in step 1 from the
  package folder.
- **"cannot find" an IR** named in a `.cfg`: paths in a `.cfg` are relative
  to the `.cfg` file's folder. Run the commands from the package folder
  exactly as written.
- **`--compare` prints `Different`:** the line after it says why. Check that
  you compared each output with the `expected.wav` in its own folder.
- Anything else: [troubleshooting](troubleshooting.md).
