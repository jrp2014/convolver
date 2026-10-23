<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# First-use examples

Three small filters to try the command-line renderer on, each with an input
and the output it should produce. The same files drive every plug-in adapter's
comparison tests, so a filter sounds the same in the CLI, VST3, LV2 and AU.

| Folder | Filter | What it shows |
| --- | --- | --- |
| `mono/` | `impulse.wav`, a mono IR used directly | The simplest filter: one IR file, no configuration |
| `stereo/` | `filter.cfg` with the two-channel `room.wav` | A different response per side, left fed into the right output at gain 0.5, and the right output delayed by 1 ms |
| `crossover/` | `filter.cfg` with the six-channel `crossover.wav` | Stereo in, six outputs out (2→6): low, mid and high bands per side, tweeters 6 dB lower and 0.5 ms later |

Each folder also holds `input.wav` (85 ms of white noise per input channel,
peaking at half full scale, so turn the volume down before listening) and `expected.wav`, the result of filtering that input with a direct
double-precision convolution, the project's reference for correctness.

## Try them

Build the CLI first ([Build and try it](../../README.md#build-and-try-it)), then,
from the repository root:

<!-- first-use-example -->
```sh
./build/convolver --filter examples/first-use/mono/impulse.wav --input examples/first-use/mono/input.wav --output mono.wav
./build/convolver --filter examples/first-use/stereo/filter.cfg --input examples/first-use/stereo/input.wav --output stereo.wav
./build/convolver --filter examples/first-use/crossover/filter.cfg --input examples/first-use/crossover/input.wav --output crossover.wav
```

Each command writes a 32-bit float WAV with the processing delay removed and
the whole filter tail kept, so `mono.wav` matches `mono/expected.wav` sample for
sample, to within about 1e-7 (single-precision arithmetic). To see how a
configuration is read before rendering with it:

<!-- first-use-example -->
```sh
./build/convolver --validate examples/first-use/crossover/filter.cfg
./build/convolver --inspect examples/first-use/stereo/filter.cfg
```

The `.cfg` files are commented line by line; the
[configuration reference](../../docs/configuration.md) explains every field. The
crossover's bands are deliberately simple near-Gaussian filters that add back
to the original signal: they show routing, not a crossover ready for real
speakers.

## How these files are checked

The `examples.first_use` test runs the commands above exactly as written, with
the CLI that was just built, and compares every output with `expected.wav`
(at most 1e-5 apart). The plug-in tests then load the same filters through each
plug-in format and require output identical to those CLI renders, once the
plug-in's 256-frame latency is allowed for, and within 1e-5 of the reference;
see [plug-in comparisons](../../docs/develop/testing.md#plug-in-comparisons-plug-2).
`examples.fixtures` checks that the files here are exactly what the generator
writes.

## Provenance and licence

Every file here is synthetic, written by
[`tests/plugins/fixtures.py`](../../tests/plugins/fixtures.py) from integer
arithmetic and a fixed-seed generator, without recorded or third-party audio.
They were written for this project and are licensed GPL-2.0-or-later, like the
rest of Convolver. To regenerate them after changing the generator:

```sh
python3 tests/plugins/fixtures.py write examples/first-use
```
