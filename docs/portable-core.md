# Portable core and CLI

[Documentation index](README.md) · [Concepts](concepts.md) · [Configuration format](configuration.md)

This is the detailed reference for building and using the CLI and C++ core.
For a first render, use the [quick start](../README.md#build-and-try-it).
The active implementation is C++20 with FFTW, Apple Accelerate or built-in Ooura;
file I/O uses libsndfile. It does not depend on the old Windows applications.

**In short:**

- `convolver --filter IR_OR_CFG --input IN --output OUT` renders a file. The
  output lines up with the input (processing latency removed) and keeps the
  whole filter tail. Rate and channel counts must match; nothing is resampled.
- Output is 32-bit float WAV by default. Integer WAV (8 to 32 bits) is plainly
  rounded unless you ask for `--dither`, `--noise-shaping` or `--valid-bits`.
- `--gain-db` takes a fixed decibel value, `auto` (noise-based estimate) or
  `no-clip` (worst-case filter bound). Neither estimate is a limiter.
- `--inspect` and `--validate` explain what a configuration resolves to without
  rendering; `--json` gives a machine-readable report.
- The FFT backend is chosen when building. FFTW builds can save tuned plans
  ("wisdom") with `--tune-fft` and reuse them with `--fft-wisdom`.

Contents:

- [Build and backend selection](#build), [FFTW plan tuning](#fftw-plan-tuning), [Windows](#windows-build)
- [File rendering](#file-rendering): [gain](#gain-and-attenuation),
  [integer output and dither](#integer-formats-dither-and-noise-shaping),
  [speaker labels](#speaker-labels), [output safety](#output-safety)
- [RAW streams](#raw-streams)
- [Inspection and validation](#inspecting-and-validating-configurations)
- [Library API](#engine-api) and [resource limits](#resource-limits)

## Build

Install CMake 3.20+, a C++20 compiler, FFTW's single-precision development library
and libsndfile's development library. The default tests also need Python 3.8+.
For example:

```sh
# macOS with Homebrew
brew install cmake fftw libsndfile
# Debian/Ubuntu
sudo apt-get install cmake g++ python3 libfftw3-dev libsndfile1-dev
```

Then, from the repository root:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
./build-native/convolver --version
```

Use `-DCMAKE_PREFIX_PATH=/path/to/dependencies` for a separate installation.
Configuration never downloads dependencies. CMake finds FFTW3f/SndFile package
configs, with library/header discovery fallbacks (and pkg-config for libsndfile).
For the default FFTW backend, its single-precision library is required even if
double FFTW is installed. On Apple platforms select the system framework instead:

```sh
cmake -S . -B build-accelerate -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_FFT_BACKEND=ACCELERATE
cmake --build build-accelerate --parallel
ctest --test-dir build-accelerate --output-on-failure
```

Accelerate builds neither search for nor link FFTW. `-DCONVOLVER_FFT_BACKEND=OOURA`
builds Ooura's FFT into the library instead, on any platform, so the only
remaining dependency is libsndfile for the CLI and file I/O. Backend selection is
per build, not a runtime switch; `--version` identifies the selected backend.
See [FFT contracts, benchmarks and equivalence results](fft-backends.md).

### FFTW plan tuning

An optimized FFTW build supplies SIMD routines; measured planning selects among
them for the transform size actually used. Run this once on the target machine
with a Release binary and little competing CPU activity:

```sh
./build-native/convolver --tune-fft machine-b256.fftw --block-size 256
./build-native/convolver --filter room.cfg --input music.wav --output corrected.wav \
  --block-size 256 --fft-wisdom machine-b256.fftw
```

Tuning needs no input audio or IR. It prepares both transforms using `patient`
planning by default; `--fft-plan measure` searches less extensively and
`--fft-plan exhaustive` tries every algorithm FFTW has, which can take much
longer for large blocks. `--fft-time-limit S` bounds each plan's search to S
seconds and keeps the best plan found by then. For example,
`--fft-plan patient --fft-time-limit 30` gives each plan a 30-second budget. A search the limit
cuts short may not be stored as reusable wisdom for that mode; tune without a
limit when the wisdom is for `--fft-wisdom`. The wisdom
destination must be a **new file**; existing files and aliases are never replaced.
Use a different filename when retuning, then replace an older cache deliberately.

Normal rendering defaults to `estimate`, or accepts
`--fft-plan measure|patient|exhaustive` (optionally with `--fft-time-limit`) to
time plans during startup. `--fft-wisdom FILE` imports native single-precision
FFTW wisdom and requires matching plans, with no silent measured fallback. A
missing size or insufficient planning rigor fails before opening the audio
output. Wisdom of one rigor satisfies requests of lower rigor (exhaustive covers
patient, patient covers measure and estimate), not higher. CLI wisdom input is limited to 1 MiB.

Keep wisdom with the CPU, FFTW build, precision and block size it was tuned for.
Retune after changing them; loading successfully is **not** proof that old wisdom
is optimal for a different CPU. The native wisdom format performs FFTW's own
compatibility checks; Convolver does not add a CPU fingerprint. Timing is noisy
and does not guarantee a globally optimal plan. This step does not choose B or
change B-frame latency. Accelerate and Ooura reject measured planning, wisdom and time-limit options.
See [FFTW's planning/wisdom explanation](https://fftw.org/fftw3_doc/Words-of-Wisdom_002dSaving-Plans.html).

Library callers can pass `FftOptions` to `Engine`, or call `tune_fft`, then
`export_fft_wisdom`/`import_fft_wisdom`. Core APIs use strings, with file handling
kept in the CLI. Wisdom is global to the FFTW library in the process. Convolver
serializes its own planning, destruction and wisdom access; applications making
other FFTW planner calls must coordinate those too. All preparation belongs
outside the audio callback.

### Windows build

Windows/MSVC configuration, using an existing vcpkg installation:

```powershell
vcpkg install fftw3:x64-windows 'libsndfile[core]:x64-windows'
cmake -S . -B build-native -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DCONVOLVER_LEGACY_TESTS=OFF
cmake --build build-native --config Release
ctest --test-dir build-native -C Release --output-on-failure
```

The [native CI workflow](../.github/workflows/regression.yml) defines the platform
matrix. Check [Actions](https://github.com/jrp2014/convolver/actions/workflows/regression.yml)
for revision-specific results. Package-manager dependency versions can change;
`--version` records the linked versions. Pin dependencies before producing a
reproducible binary release.

Targets: `convolver_core`, `convolver_host`, `convolver_io`, `convolver_cli` (executable name
`convolver`). `cmake --build build-native --target check` builds native tests and
runs CTest. `cmake --install build-native --prefix /desired/prefix` installs the
CLI, libraries and headers. Libraries can also be consumed with `add_subdirectory`
through `Convolver::core`, `Convolver::host` and `Convolver::io`. Installed CMake package exports,
component selection and presets are described in [the package guide](cmake-package.md).
The libraries are static; shared dependencies must remain installed. This is not
a self-contained redistributable app bundle.

Build options:

| Option | Default | Purpose |
| --- | --- | --- |
| `CONVOLVER_BUILD_NATIVE` | ON | Enable portable targets; OFF retains regression-only mode |
| `CONVOLVER_BUILD_IO` | ON | libsndfile and configuration adapter |
| `CONVOLVER_BUILD_CLI` | Same as I/O on first configure | Renderer; requires I/O |
| `CONVOLVER_FFT_BACKEND` | AUTO | AUTO (FFTW; OOURA, or ACCELERATE on Apple, when `CONVOLVER_BUILD_VST3` is ON), FFTW, ACCELERATE (Apple only) or OOURA (built in); see [plug-in builds](fft-backends.md#plug-in-builds) |
| `CONVOLVER_BUILD_VST3` | OFF | SDK adapter and, with I/O, the preset writer; see [VST3](vst3.md) |
| `CONVOLVER_BUILD_FUZZERS` | OFF | Clang libFuzzer targets; requires I/O |
| `CONVOLVER_BUILD_BENCHMARKS` | OFF | Build `convolver_benchmark` |
| `BUILD_TESTING` | ON at top level | Master test switch |
| `CONVOLVER_BUILD_TESTS` | ON at top level; OFF as a subproject | Convolver test targets, subject to `BUILD_TESTING` |
| `CONVOLVER_PACKAGE_TESTS` | ON | Installed/relocated and source-tree C++ consumer tests |
| `CONVOLVER_DOCUMENTATION_TESTS` | Same as I/O on first configure | Historical documentation checks; requires Python |
| `CONVOLVER_SANITIZERS` | OFF | ASan/UBSan instrumentation of native targets with GCC/Clang |
| `CONVOLVER_LEGACY_TESTS` | ON with I/O and native Clang on macOS/Linux; OFF elsewhere | Existing sanitized extraction harnesses |

## File rendering

```sh
./build-native/convolver --filter room.cfg --input music.wav --output corrected.wav
./build-native/convolver --filter impulse.wav --input music.wav --output corrected.wav --block-size 128 --gain-db -6 --output-format wav-pcm24
```

The filter may be an IR sound file, a [configuration](configuration.md), or a configuration
list. A direct multichannel IR maps each IR channel to the corresponding input
and output; a mono IR requires mono input. Use a config to apply one mono IR to
several channels. Lists select the first entry matching input rate and channel
count. `--output-channels N` adds the requirement of N output channels; a
single configuration or sound file that has a different count is rejected (a
sound file has as many outputs as inputs). There is no resampling or
nearest-rate substitution.

`--block-size` is the uniform partition payload B, not the historical partition
count. FFTW uses FFT length 2B; Accelerate and Ooura round up to a power of two
(at least four). Positive non-power-of-two B values work with all three backends. The core
has B frames of algorithmic latency. The CLI trims that leading latency by
default and always drains the complete FIR/routing tail. `--keep-latency` retains
it for callers that need the untrimmed stream. No input frames means no
output audio frames. Gain is a finite decibel value; finite float output can
exceed unity. Integer output clips to its representable range.

### Gain and attenuation

`--gain-db NUMBER` applies a fixed decibel gain. Negative values attenuate;
positive values amplify. Two offline estimates are also available:

- `--gain-db auto` measures the peak response to seeded uniform noise in [-1, 1)
  on every input, for ten filter lengths (at least 65,536 frames). It applies
  `-20 log10(peak)`. This estimates a useful level but does not bound peaks in
  arbitrary programme material.
- `--gain-db no-clip` calculates a mathematical filter bound: for each output,
  combine the paths from each input, sum the absolute values of their taps,
  then take the largest output sum. The gain is `-20 log10(bound)`, or 0 dB for
  silent routing. It assumes every input sample has magnitude at most 1.

Both report the chosen gain on stderr and may take time before rendering. The
bound applies to ideal linear filtering, not input above full scale or additional
quantizer noise. Floating-point error, dither, noise shaping and the asymmetric
positive integer endpoint need headroom if clipping must be strictly avoided;
see **GAIN-1** in [TODO](../TODO.md). Neither option is a limiter.
The API is `estimate_attenuation` in [engine.hpp](../include/convolver/engine.hpp);
[attenuation.cpp](../portable/attenuation.cpp) and the engine oracle tests define
and exercise the calculation. The noise estimate retains the older engine's
"optimum attenuation" method.

### Integer formats, dither and noise shaping

Outputs: `wav-float` (default), `wav-pcm8` (unsigned, as WAV defines 8-bit PCM),
`wav-pcm16`, `wav-pcm24`, `wav-pcm32`, `raw-float`.

Every integer format is quantized by Convolver's own `Quantizer`, not libsndfile:
with B significant bits, each sample is scaled by 2^(B-1), offset by the dither
(in LSBs) and the noise-shaping feedback, rounded to the nearest integer (ties
toward +infinity) and clipped to [-2^(B-1), 2^(B-1) - 1]. PCM16 output is
unchanged from 0.3 (`Pcm16Quantizer` is now the 16-bit case). PCM24 and PCM32
used libsndfile's conversion before 0.4; they now follow the same rule, which
can differ from it by at most one LSB.

Dither is off by default. `--dither` selects `none` (default), `triangular`
(independent TPDF, error power 1/4 LSB² at every signal level),
`triangular-highpass` (the historical engine's "Triangular": per-channel
difference of successive uniform values, same amplitude, high-pass error
spectrum) or `rectangular`. `--dither-seed N` (default 0) seeds the generator;
the same seed, input and channel count give byte-identical output regardless of
buffering. Float and RAW formats reject dither, noise shaping and valid bits.

`--noise-shaping` selects one of the historical engine's error-feedback shapers:
`simple` (first order), `second-order`, `third-order`, `ninth-order` or
`sony-sbm` (the historical "Pseudo Sony SBM"), or `none` (default). Each
subtracts a fixed combination of the channel's previous quantization errors
before rounding, so the error spectrum is the rounding error filtered by
1 - Σ c_k z^-k, redistributing noise across frequency, often at the cost of more total noise.
The coefficients are fixed; their audible effect depends on sample rate and
playback conditions. The fed-back error is taken before
clipping, so overload cannot make a shaper run away (the historical code could
wrap). The coefficients are in [io.hpp](../include/convolver/io.hpp). Shaping works with or
without dither; use it with dither.

`--valid-bits N` quantizes to N significant bits (8 up to the container size)
and stores them left-justified with the low bits zero, in a
WAVE_FORMAT_EXTENSIBLE header that declares N as `wValidBitsPerSample`, for
example 20 valid bits in `wav-pcm24` or 24 in `wav-pcm32`, as the historical
engine offered. Dither and shaping then act at that resolution.

The library's `AudioWriter` defaults to plain rounding; pass `QuantizationOptions`
to enable dither, shaping or valid bits. The tests check rounding/clipping
endpoints for 8 to 32 bits, reproducibility, chunk independence, measured
dither error mean, variance and correlation, that each shaper's output error
equals a bounded rounding residual through the historical coefficients, that its
measured error spectrum follows 1 - Σ c_k z^-k within 15%, recovery after
clipping, and exact stored samples and header fields for every container and
valid-bit combination.

### Speaker labels

WAV output has no speaker labels unless requested. A valid-bit override may
also require an extensible header, independently of labels.
`--speaker-map config` labels the output channels with the configuration's hex
channel mask (the fourth header value) by writing a WAVE_FORMAT_EXTENSIBLE header:

```sh
./build-native/convolver --filter surround.cfg --input music.wav --output out.wav --speaker-map config
```

- The mask uses the WAVE speaker bits (`0x1` front left, `0x2` front right,
  `0x4` centre, `0x8` LFE, `0x10`/`0x20` back left/right, `0x200`/`0x400` side
  left/right, up to `0x20000` top back right). Output channel N takes the Nth set
  bit counting from bit 0, as WAVE defines, so `3F` labels six channels as
  FL FR FC LFE BL BR. Samples are never reordered: put the paths' outputs in that
  order in the configuration.
- The mask must name exactly as many speakers as there are output channels, and
  only bits 0 to 17 are accepted. Otherwise the render stops before the output is
  created.
- A mask of 0 (and any sound-file filter, which has no mask) means unspecified:
  the CLI says so on stderr and writes the plain header.
- `raw-float` output has no header, so it cannot be combined with the flag.
- After writing, the CLI reads the finished header back and fails if the labels
  are not exactly the requested mask, so a libsndfile that cannot write them is
  reported rather than silently ignored.

The library equivalent is `AudioWriter(path, info, format, {}, OutputMetadata{mask})`;
the default `OutputMetadata{}` keeps the plain header.

### Output safety

Output paths must differ from input, configuration/list files and referenced IRs,
including filesystem aliases. A regular file redirected to stdin is protected
from being opened as output by the CLI. Shell redirection itself happens before
the program starts: never redirect stdout onto an input/configuration/IR file.
Failures return a nonzero exit code and a diagnostic on stderr. Output may be
partial after an I/O or numerical failure; writing is not transactional.

## RAW streams

Standard streams use explicitly described little-endian IEEE float32, interleaved
by frame. No WAV header needs rewriting on a non-seekable pipe:

```sh
producer | ./build-native/convolver --filter room.cfg --input - --raw-input 48000 2 --output - --output-format raw-float | consumer
```

`--raw-input RATE CHANNELS` also works on a RAW file. The output rate and channel
count come from the selected configuration. No diagnostics are written to binary
stdout during rendering. Incomplete RAW frames, nonfinite samples, broken pipes,
short writes and detected finalization failures are errors. Caller-owned standard
descriptors are not closed by the I/O wrappers.

On Windows, RAW stdin uses the binary CRT reader to handle normal pipe EOF;
libsndfile 1.2.2's Windows reader reports that condition as a system error. Files
and output streams still use libsndfile. Partial-byte reads are accumulated before
checking complete frames, so fragmented writes do not lose audio.

### With FFmpeg

FFmpeg can decode an audio track, pass it through Convolver and remux it with
the original video. This example uses a 48 kHz configuration with two inputs
and two outputs, processing the first audio track and retaining the first
video track:

```sh
ffmpeg -i movie.mkv -map 0:a:0 -f f32le -ar 48000 -ac 2 - \
  | convolver --filter room.cfg --input - --raw-input 48000 2 \
      --output - --output-format raw-float \
  | ffmpeg -i movie.mkv -f f32le -ar 48000 -ac 2 -i - \
      -map 0:v:0 -map 1:a:0 -c:v copy -c:a flac movie-corrected.mkv
```

The raw pipe carries one audio stream. Change `0:a:0` to `0:a:1` to select the
second audio track; do not use `0:a`, which selects all audio tracks. Other
audio/video tracks, subtitles and attachments are omitted by this example.
See FFmpeg's [stream-selection rules](https://ffmpeg.org/ffmpeg.html#Stream-selection).

Both `-ar` values and the rate in `--raw-input` must match the configuration's
rate. The first `-ac` and the channel count in `--raw-input` must match its
**input** count; the receiving FFmpeg's `-ac` must match its **output** count.
For a 2-in/6-out configuration, keep the first `-ac 2` and `--raw-input 48000 2`,
but change the receiving command to `-ac 6`. RAW carries no channel labels;
ensure its assumed channel order matches the configuration's routes.

The new audio keeps the filter tail, so it can run past the video; add
`-shortest` to cut it. It starts at time zero, so check sync for files whose
audio starts late. A built-in FFmpeg filter is planned, not implemented: see the
[FFmpeg filter plan](superpowers/plans/2026-10-21-ffmpeg-filter.md).

Ordinary RIFF/WAVE inputs receive extra chunk-boundary and PCM/float alignment
validation, because libsndfile can otherwise silently accept truncation. Other
input containers, including RF64, retain libsndfile's validation semantics; this
is not a complete validator for every supported codec. Additional codecs depend
on how the installed libsndfile was built. Output is currently ordinary WAV or
RAW; use RAW for streams that can exceed WAV's 4 GiB container limit.

## Inspecting and validating configurations

`--inspect` shows what a configuration, sound file or list resolves to without
rendering; `--validate` checks it and prints a one-line result. Both use the
loader's own parser, list selection and limits, decode the selected IRs, and run
the engine's validation and storage accounting. Neither plans FFTs, changes FFTW
wisdom or writes files.

```sh
convolver --inspect room.cfg
convolver --inspect filters.txt --sample-rate 48000 --input-channels 2 --json
convolver --validate room.cfg --block-size 256
convolver --validate filters.txt --all-configurations --json
convolver --inspect room.cfg --syntax-only --json
```

- Selection matches rendering: the first list entry matching `--sample-rate`,
  `--input-channels` and `--output-channels` (or the first entry with none). The
  report lists each visited file and why it was skipped, for example
  `rate-and-output-mismatch`. Unselected entries' IR files are not opened.
- `--all-configurations` validates every leaf of a list, continues after a failed
  entry, and fails if any entry fails. It cannot be combined with selectors.
- `--syntax-only` reads one configuration file's text, checks grammar, routes and
  delays and resolves references lexically, without opening IRs. Coefficient,
  latency, tail and storage fields are null, and the result says so.
- `--profile portable-paths` adds warnings for references that work here but
  will not survive copying to another machine; the default profile is
  `convolver`. See [profiles](configuration-compatibility.md#profiles).
- `--json` prints one UTF-8 JSON document (report schema 1) on stdout, also when
  validation fails. Diagnostics are also printed to stderr.
- `convolver --inspect --help` lists every inspection option.
- Exit status: 0 valid, 1 invalid or unreadable, 2 invalid usage.
  `--warnings-as-errors` makes warnings fail.

A report gives, per inspected configuration, the rate, channel counts and raw
channel mask; declared delay tokens and milliseconds beside effective frames;
ordered routes with their source tokens and effective channel/gain (`-0` is
channel 0, gain -1); each IR's spelling, line, resolved path, channel, rate and
decoded length; partitions per path; latency (one block) and the FIR/delay tail
separately; and `accounted_buffer_bytes`, the engine's prepared float buffers
(an upper bound: paths with the same input mix share their history). That
estimate excludes FFT plan internals, allocator overhead and file decoding
buffers; it is not total process memory. Diagnostics carry a stable `code`,
severity, message, file, line, column and the chain of enclosing list entries.
Resolved paths are machine-specific; source spellings are kept as written.

[Configuration compatibility](configuration-compatibility.md) lists what the corpus
verifies, the `portable-paths` profile's warnings and what is known about other
programs that read the format.

The same report is available to C++ code through `convolver::inspect_configuration`
in `convolver/inspection.hpp` (`Convolver::io`), and the core's
`convolver::analyze_configuration` returns latency, tail, FFT length, accounted
storage and partitions for an in-memory `Configuration`.

## Compatibility and library API

The [configuration reference](configuration.md) owns the text grammar, path rules,
route encoding and list-selection semantics. The [compatibility matrix](configuration-compatibility.md)
records tested fixtures and the limits of claims about other programs.

### Engine API

[engine.hpp](../include/convolver/engine.hpp) exposes Configuration, FilterPath, Route and Engine.
Construction prepares FFT plans and all processing buffers. `process` takes
interleaved float spans and any frame count, producing the same number of output
frames. Empty input means silence for drain. Input/output spans must not overlap;
there is no in-place mode. `latency_frames()` and `tail_frames()` are separate;
feed their sum as silence after the final nonempty input to drain fully.
`reset()` clears history and delays. Instances have separate state; an individual
instance must not be processed/reset concurrently.

This implementation uses uniform partitioned overlap-add with input-spectrum
history, as described in [the implementation map](project-structure.md#processing-data-flow). All three backends expose the same spectrum layout and
scaling; 1/N normalization is applied once when preparing IR spectra. The first
backend measurements are recorded in [the FFT guide](fft-backends.md).

Prepared processing/reset performs no C++ heap allocation; a test guards that
contract. This is not a hard real-time guarantee or instrumentation of every
possible library/system allocation. FFTW planning/destruction is serialized between
these engine instances, outside processing. Accelerate owns an independent setup
per instance. Other FFTW users in the same process
must coordinate their planning too, which is why plug-in builds default to Ooura
or Accelerate ([plug-in builds](fft-backends.md#plug-in-builds)).

### Resource limits

Resource limits are deliberate: 1..256 channels, 1..768000 Hz, B up to 1048576,
4096 paths/routes, and about 1 GiB of prepared float-equivalent storage. Core IRs
and individual delays are bounded to 67108864 frames. The importer additionally
limits each text file to 1 MiB and all text read per operation to 16 MiB, list nesting to 16, visited entries to 4096, total decoded IR
samples to 64 Mi, and each delay to 16 Mi frames. Invalid dimensions, arithmetic
overflow and nonfinite inputs/IRs/gains are rejected. DSP overflow is detected by
the CLI's output check; library callers choose their own output policy.

## Verification and dated dependency record

The native tests link actual production translation units and actual libraries:
independent double time-domain convolution, seeded matrices/delays, awkward IR
lengths/block sizes, chunk invariance, reset, tails, file round trips, full-width
PCM32 endpoints, legacy parsing and actual RAW descriptors/subprocess pipelines.
Fault tests cover source overwrite protection, truncation, fragmented pipes,
write failure and WAV-header finalization. Existing historical tests and archived
website integrity checks remain enabled on supported Clang hosts.

The following sources describe the earlier October 2026 local test setup,
not dependency pins for every current build:

- FFTW 3.3.11, archive SHA256
  `5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1`.
- libsndfile 1.2.2, archive SHA256
  `3799ca9924d3125038880367bf1468e53a1b7e3686a934f098b7e1d286cdb80e`;
  optional external codecs disabled for this local test installation.

FFTW's 3.3.11 CMake sources still set their reported version to 3.3.10. That
local verification used the official configure/make build of that source archive,
which reports 3.3.11. Convolver itself is built with CMake in both cases.
See [test coverage](testing.md) and [TODO limitations](../TODO.md#documented-limitations)
for the boundaries of the current checks. Current LLVM/SDK/library results must
be tied to the revision actually tested.
