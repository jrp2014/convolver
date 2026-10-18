# First audible output

Convolver 0.2.0 developer preview needs a VST3 host. Installing a plug-in does
not add convolution to an ordinary player or route system audio automatically.
No commercial host is currently qualified; use the host's normal VST3 preset loader.

1. Select a mono or stereo track at 44100, 48000 or 96000 Hz.
2. Load Convolver and import the matching `presets/identity-RATE-LAYOUT/filter.vstpreset`.
3. Play quiet audio and check input/output meters. Identity preserves the signal
   apart from 256 samples of latency; the host may compensate for it.
4. Import the matching `echo` example. It has taps 0.5 at frame 0 and 0.25 at
   frame 257. Leave headroom for your own filters; there is no automatic normalization.
5. Toggle the host's generic Convolver bypass parameter. Save, close and reopen
   the project. Filters are embedded, so the source WAV need not accompany it.

From the extracted package directory, create a new preset (the output must not exist):

```sh
./bin/convolver-vst3-preset presets/echo-48000-stereo/impulse.wav room.vstpreset
```

In PowerShell use `./bin/convolver-vst3-preset.exe` with the same two arguments.
A supported legacy `.cfg` or configuration-list file can replace the WAV argument.
A list selects its first matching entry with unspecified rate/layout; use an
individual configuration when you need a particular alternative. Mono/stereo input
and output counts must match. Match the host's rate: filters are never resampled.

Internal processing is single precision, including with double host buffers.
Silent input drains the tail. Changing filters starts new history and crossfades;
a newly loaded filter cannot process audio that preceded loading. See
[troubleshooting](troubleshooting.md) for recovery and [installation](install.md).
