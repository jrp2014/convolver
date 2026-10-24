<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Command-line reference

[Using Convolver](README.md) · [Concepts](concepts.md) · [Configuration files](configuration.md)

For anyone filtering files or streams with `convolver`: every option, output
format and check. New to it? Start with [your first filter](first-filter.md).
To build it, see [build from source](../develop/build.md).

**In short:**

- `convolver --filter IR_OR_CFG --input IN --output OUT` renders a file. The
  output lines up with the input (processing latency removed) and keeps the
  whole filter tail. Rate and channel counts must match; nothing is resampled.
- Output is 32-bit float WAV by default. `--output-format` also writes RF64,
  W64, AIFF, CAF, FLAC and raw files, in 8- to 32-bit integer or 32- and 64-bit
  float. Integer output is plainly rounded unless you ask for `--dither`,
  `--noise-shaping` or `--valid-bits`.
- `--gain-db` takes a fixed decibel value, `auto` (noise-based estimate) or
  `no-clip` (worst-case filter bound). Neither estimate is a limiter.
- `--inspect` and `--validate` explain what a configuration resolves to without
  rendering; `--json` gives a machine-readable report.
- FFTW builds can save tuned FFT plans ("wisdom") with `--tune-fft` and reuse
  them with `--fft-wisdom`.

Contents:

- [File rendering](#file-rendering): [gain](#gain-and-attenuation),
  [output formats](#output-formats),
  [integer output and dither](#integer-formats-dither-and-noise-shaping),
  [speaker labels](#speaker-labels), [output safety](#output-safety)
- [RAW streams](#raw-streams) and [FFmpeg](#with-ffmpeg)
- [Inspection and validation](#inspecting-and-validating-configurations)
- [FFTW plan tuning](#fftw-plan-tuning)

The examples assume `convolver` is on your `PATH`; from a source build, use
`./build/convolver`. `convolver --version` names the FFT library it was built
with, and `convolver --help` lists every option.

## File rendering

```sh
convolver --filter room.cfg --input music.wav --output corrected.wav
convolver --filter impulse.wav --input music.wav --output corrected.wav --block-size 128 --gain-db -6 --output-format wav-pcm24
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
quantizer noise. Floating-point error, dither, noise shaping and the largest
positive integer value (one step short of full scale) need a little more
headroom if clipping must be strictly avoided. Neither option is a limiter.
The noise estimate is the old Windows Convolver's "optimum attenuation"
method.

### Output formats

`--output-format CONTAINER-TYPE` picks the file type and the sample type, for
example `flac-pcm24` or `rf64-float`. The default is `wav-float`.

| Container | Use it for | Sample types |
| --- | --- | --- |
| `wav` | Ordinary WAV, up to 4 GiB | `pcm8`, `pcm16`, `pcm24`, `pcm32`, `float`, `double` |
| `rf64`, `w64` | WAV-style files larger than 4 GiB | the same |
| `aiff` | Apple and older Mac software, up to 4 GiB | the same |
| `caf` | Core Audio Format, no size limit | the same |
| `flac` | Lossless compression, about half the size | `pcm8`, `pcm16`, `pcm24` |
| `raw` | Headerless little-endian samples, for pipes | `pcm16`, `pcm24`, `pcm32`, `float`, `double` |

`float` is 32-bit and `double` 64-bit IEEE floating point. 8-bit PCM is
unsigned in `wav`, `rf64` and `w64`, as those formats define it, and signed in
`aiff`, `caf` and `flac`. An unsupported pair, such as `flac-float`, is
rejected before any file is created. Speaker labels (`--speaker-map`) and
`--valid-bits` below the container size need a `wav-` format, because only
WAVE_FORMAT_EXTENSIBLE carries them here. Standard output (`--output -`)
needs a `raw-` format, since no file header can be completed on a pipe.

FLAC output needs a libsndfile built with FLAC; see [FLAC and Ogg](#flac-and-ogg).

### FLAC and Ogg

The [preview packages](install.md#what-a-preview-package-contains) contain a libsndfile built
without its optional codec libraries (libFLAC, libogg, libvorbis and libopus),
and will stay that way, to keep the packages free of extra dependencies
(owner's decision, 24 October 2026). So
packaged copies of Convolver, the command line and the plug-ins alike, cannot
read FLAC or Ogg files (input or IR) or write FLAC. `convolver --version` says
whether the libsndfile in use has FLAC (`libsndfile ... (no FLAC output)`
otherwise), and without it a `flac-` output format, or a FLAC input or IR, is
refused with a message before any file is created.

To work with FLAC anyway, do one of these:

- **Convert at the edges.** Decode FLAC to WAV first and encode the result
  afterwards, with the reference [`flac`](https://xiph.org/flac/) tool, SoX or
  FFmpeg:

  ```sh
  flac --decode music.flac -o music.wav
  convolver --filter room.cfg --input music.wav --output corrected.wav --output-format wav-pcm24
  flac --best corrected.wav -o corrected.flac
  ```

  Or stream through a pipe, so no intermediate files are written (the `-ar`
  and `-ac` values must match the configuration, as in
  [RAW streams](#raw-streams)):

  ```sh
  ffmpeg -i music.flac -f f32le -ar 48000 -ac 2 - \
    | convolver --filter room.cfg --input - --raw-input 48000 2 --output - --output-format raw-float \
    | ffmpeg -f f32le -ar 48000 -ac 2 -i - -c:a flac -sample_fmt s32 corrected.flac
  ```

  FIR filters in FLAC are converted once with `flac --decode`; the plug-ins
  need this too.
- **Build Convolver against a libsndfile that has FLAC.** The libsndfile
  packages of Linux distributions (`libsndfile1-dev` on Debian and Ubuntu),
  Homebrew (`libsndfile`) and vcpkg (`libsndfile` with its default features)
  include FLAC and Ogg. [Build from source](../develop/build.md) with one of
  them, then check that `convolver --version` no longer says `no FLAC output`.

### Integer formats, dither and noise shaping

Every integer sample type is quantized by Convolver in the same way, whatever
the container; `wav-pcm8`, `wav-pcm16`, `wav-pcm24` and `wav-pcm32` are the
examples below.

Convolver converts to every integer format itself: with B significant bits,
each sample is scaled by 2^(B-1), offset by the dither (in LSBs, least
significant bits) and the noise-shaping feedback, rounded to the nearest
integer (ties toward +infinity) and clipped to [-2^(B-1), 2^(B-1) - 1]. PCM24
and PCM32 output before version 0.4 used libsndfile's conversion, which can
differ by at most one LSB.

Dither is off by default. `--dither` selects `none` (default), `triangular`
(independent TPDF, error power 1/4 LSB² at every signal level),
`triangular-highpass` (the historical engine's "Triangular": per-channel
difference of successive uniform values, same amplitude, high-pass error
spectrum) or `rectangular`. `--dither-seed N` (default 0) seeds the generator;
the same seed, input and channel count give byte-identical output regardless of
buffering. Float and double formats reject dither, noise shaping and valid
bits.

`--noise-shaping` selects one of the historical engine's error-feedback shapers:
`simple` (first order), `second-order`, `third-order`, `ninth-order` or
`sony-sbm` (the historical "Pseudo Sony SBM"), or `none` (default). Each
subtracts a fixed combination of the channel's previous quantization errors
before rounding, so the error spectrum is the rounding error filtered by
1 - Σ c_k z^-k, redistributing noise across frequency, often at the cost of more total noise.
The coefficients are fixed; their audible effect depends on sample rate and
playback conditions. The fed-back error is taken before
clipping, so overload cannot make a shaper run away (the historical code could
wrap). Shaping works with or without dither; use it with dither.

`--valid-bits N` quantizes to N significant bits (8 up to the container size)
and stores them left-justified with the low bits zero, in a
WAVE_FORMAT_EXTENSIBLE header that declares N as `wValidBitsPerSample`, for
example 20 valid bits in `wav-pcm24` or 24 in `wav-pcm32`, as the historical
engine offered. Dither and shaping then act at that resolution.

### Speaker labels

WAV output has no speaker labels unless requested. A valid-bit override may
also require an extensible header, independently of labels.
`--speaker-map config` labels the output channels with the configuration's hex
channel mask (the fourth header value) by writing a WAVE_FORMAT_EXTENSIBLE header:

```sh
convolver --filter surround.cfg --input music.wav --output out.wav --speaker-map config
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
- Only `wav-` formats carry the labels; any other container stops the render
  before the output is created (labels in other containers are TODO IO-5).
- After writing, the CLI reads the finished header back and fails if the labels
  are not exactly the requested mask, so a sound-file library that cannot write
  them is reported rather than silently ignored.

### Output safety

Output paths must differ from input, configuration/list files and referenced IRs,
including filesystem aliases. A regular file redirected to stdin is protected
from being opened as output by the CLI. Shell redirection itself happens before
the program starts: never redirect stdout onto an input/configuration/IR file.
Failures return a nonzero exit code and a diagnostic on stderr. Output may be
partial after an I/O or numerical failure; writing is not transactional.

## RAW streams

Standard streams carry headerless samples, interleaved by frame, so no header
needs rewriting on a non-seekable pipe. The input's rate and channel count are
given with `--raw-input`; its sample type is little-endian 32-bit float unless
`--raw-format` says otherwise:

```sh
producer | convolver --filter room.cfg --input - --raw-input 48000 2 --output - --output-format raw-float | consumer
```

Headerless `.pcm` and `.raw` filters in a configuration are a separate
choice, `--raw-filter-format`, with the same names; see
[filter files](configuration.md#filter-files).

`--raw-format` needs `--raw-input`, since a file with a header states its own
sample type. It takes the names FFmpeg and SoX use: `f32le` (default), `f64le`,
`s16le`, `s24le` and `s32le`, or the same ending in `be` for big-endian.
Integers are scaled by 2^-(bits-1), so `s16le` 16384 is 0.5. On output, the
`raw-` formats write little-endian `pcm16`, `pcm24`, `pcm32`, `float` or
`double` (see [output formats](#output-formats)); integer output is quantized
and can be dithered like any other integer format. For example, 16-bit
samples from SoX:

```sh
sox in.flac -t raw -e signed -b 16 -L - \
  | convolver --filter room.cfg --input - --raw-input 48000 2 --raw-format s16le \
      --output out.wav --output-format wav-pcm24
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
audio starts late. FFmpeg has no built-in Convolver filter; this pipe is the way
to use the two together.

Ordinary RIFF/WAVE inputs receive extra chunk-boundary and PCM/float alignment
validation, because libsndfile can otherwise silently accept truncation. Other
input containers, including RF64, retain libsndfile's validation semantics; this
is not a complete validator for every supported codec. Additional codecs depend
on how the installed libsndfile was built. For output that can exceed 4 GiB,
use an `rf64`, `w64`, `caf`, `flac` or `raw` format; `wav` and `aiff` stop with
an error at their limit.

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
  `convolver`. See [profiles](other-programs.md#check-that-a-configuration-will-travel).
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

[Other programs](other-programs.md) lists the `portable-paths` profile's
warnings and what is known about other programs that read the format. The same
report is available to C++ programs ([library guide](../develop/library.md#inspecting-configurations)).

## FFTW plan tuning

This applies only to builds that use FFTW (`convolver --version` says which).
An optimized FFTW build supplies SIMD routines; measured planning selects among
them for the transform size actually used. Run this once on the target machine
with a Release binary and little competing CPU activity:

```sh
convolver --tune-fft machine-b256.fftw --block-size 256
convolver --filter room.cfg --input music.wav --output corrected.wav \
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

## Limits

Up to 256 channels, sample rates up to 768 kHz and block sizes up to 1048576
frames. A configuration has at most 4096 paths, each delay at most 16777216 frames,
and all its IRs together at most 67108864 samples; the [library guide](../develop/library.md#resource-limits) has
the complete list. What Convolver does not do is in [limits](limits.md).
