<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# FFmpeg audio filter plan

Status: proposed work, written on 21 October 2026 against Convolver `f6ac6c6`
(package 0.4.3) and FFmpeg git master `e2798d5` (its `RELEASE` file says
`8.0.git`). Nothing here is implemented except the pipe workflow in step 1, which
already works. Open work is tracked as FFMPEG-1 to FFMPEG-5 in
[TODO](../../../TODO.md#ffmpeg-integration).

**In short:**

- FFmpeg cannot load audio filters at run time. A filter is either compiled into
  `libavfilter` or reached through one of FFmpeg's own plug-in hosts (`lv2`,
  `ladspa`). "libav" here means FFmpeg's libraries; the separate Libav fork is
  no longer maintained and is not a target.
- **Works today:** pipe raw float audio from `ffmpeg` through the Convolver CLI
  and back into `ffmpeg` (tested below, including remuxing with video).
- **Recommended next step:** a small C API over the existing `HostProcessor`
  and configuration loader, installed with a pkg-config file. Then an
  out-of-tree FFmpeg patch adding a `convolver` filter that links that library,
  following the `rubberband` filter's pattern (a C++ library behind a C header).
- **Licence:** Convolver is GPL-2.0-or-later, so the filter is only available in
  FFmpeg builds configured with `--enable-gpl`, like `rubberband`, `x264` or
  `vidstab`. That is compatible; no relicensing is needed. LGPL-only FFmpeg
  builds cannot include it.
- **Why not just use `afir`:** FFmpeg's `afir` filters channel *n* with IR
  channel *n* only. It has no routing matrix, delays, unequal input/output
  counts, `.cfg` files or list selection, normalizes IR gain by default and stops
  at the end of the input without the tail. Convolver's value is running the
  same `.cfg` files Roon, JRiver and Audiolense users already have, with the same
  output as the CLI and VST3.
- Upstreaming into FFmpeg is a later, separate decision (FFMPEG-5).

Contents: [what FFmpeg already has](#what-ffmpeg-already-has) ·
[integration options](#integration-options) · [licensing](#licensing) ·
[the C API](#c-api) · [filter design](#filter-design) · [reverb](#reverb-use) ·
[testing](#testing) · [build and distribution](#build-and-distribution) ·
[milestones](#milestones) · [decisions for the owner](#decisions-for-the-owner)

## What FFmpeg already has

Checked in FFmpeg git master (`libavfilter/af_afir.c`, `af_headphone.c`,
`af_sofalizer.c`, `af_lv2.c`, `af_ladspa.c`, `configure`, `doc/filters.texi`) and
in Ubuntu 24.04's FFmpeg 6.1.1 package, whose build has `--enable-gpl`,
`--enable-lv2` and `--enable-ladspa`.

| Filter | What it does | Limits relevant to us |
| --- | --- | --- |
| `afir` | Partitioned FIR convolution of up to 60 s IRs; IRs arrive as extra filter-graph inputs; up to 32 switchable IRs (`nbirs`, `ir` command with crossfade); float or double precision; non-uniform partitions (`minp`/`maxp`, default 8192); slice threads | One IR channel per input channel, or one mono IR for all. Output channels equal input channels. No cross-channel routing, gains or delays. IR gain is normalized by default (`irnorm=1` in master; `gtype=peak` in 6.1). IR streams are converted to the main stream's rate and format by FFmpeg's negotiation. End of input ends output; the tail is dropped. |
| `headphone` | Binaural rendering: one stereo HRIR stream per input channel, mapped by channel name | Always stereo output; HRIR-specific |
| `sofalizer` | Binaural rendering from a SOFA HRTF file (libmysofa) with virtual speaker positions | Always stereo output; SOFA only |
| `firequalizer`, `anequalizer`, `aiir` | Design and apply filters from gain curves or IIR coefficients | They design filters; we apply measured ones |
| `lv2`, `ladspa` | Host external plug-ins | See [option B](#integration-options) |

Measured on this cloud container (Intel Xeon 2.8 GHz, 4 cores, FFmpeg 6.1.1,
Convolver Release build with Clang and FFTW, one run each, so indicative only):

- Mono white noise (3 s, 48 kHz) through a 0.8 s mono IR. `afir=gtype=none`
  and the Convolver CLI both matched a double-precision FFT reference: worst
  errors 2.9e-7 and 4.3e-7 at an output peak of 1.16. They differed from each
  other by at most 5.5e-7. `afir` returned 144000 frames, the CLI 182399 (input
  plus the 38399-frame tail). `afir` with its default gain normalization
  produced output 0.0087 times as loud (-41 dB) for this IR; the factor depends
  on the IR.
- 60 s of stereo pink noise through a 3 s IR, one path per channel: `afir`
  0.81 s (default partitions) and 1.68 s (`minp=256`); Convolver CLI 3.67 s at
  B=256 and 1.03 s at B=1024, including WAV writing. A true-stereo (four-path)
  version took 7.84 s at B=256, 1.90 s at B=1024 and 0.81 s at B=4096. Long
  reverbs are where non-uniform partitions ([PERF-2](../../../TODO.md#fft-libraries-and-performance))
  would pay off; an offline filter can also simply use a larger block.

## Integration options

| Option | Work | Who can use it | Verdict |
| --- | --- | --- | --- |
| **A. Pipe through the CLI** | None; document it | Anyone with `ffmpeg` and `convolver` | Do now (FFMPEG-1) |
| **B. LV2 plug-in hosted by FFmpeg's `lv2` filter** | New LV2 adapter | FFmpeg builds with `--enable-lv2` (Ubuntu's has it) | Only as part of an LV2 adapter for other hosts (HOST-2) |
| **C. Out-of-tree FFmpeg patch + C library** | C API, filter source, patch maintenance, CI | People who build FFmpeg, or use binaries we publish | Recommended (FFMPEG-2 to FFMPEG-4) |
| **D. Submit C upstream** | Review on ffmpeg-devel or code.ffmpeg.org, ongoing maintenance | Every GPL FFmpeg build that enables it | Decide after C is proven (FFMPEG-5) |
| **E. Native C port inside libavfilter** | Rewrite the engine and `.cfg` parser in C | Every FFmpeg build | Rejected: duplicates `afir`, needs relicensing, loses our tests |

**A. Pipe.** The CLI already streams little-endian float (see
[RAW streams](../../portable-core.md#raw-streams)). Tested here with a
true-stereo `.cfg` and a 5 s video:

```sh
ffmpeg -i movie.mkv -map 0:a -f f32le -ar 48000 -ac 2 - \
  | convolver --filter hall.cfg --input - --raw-input 48000 2 \
      --output - --output-format raw-float \
  | ffmpeg -i movie.mkv -f f32le -ar 48000 -ac 2 -i - \
      -map 0:v -map 1:a -c:v copy -c:a flac movie-hall.mkv
```

The audio matched a file render of the same input to 1.2e-7 (the 24-bit FLAC
step) and kept the 0.8 s tail (add `-shortest` to cut it). `-ar` and `-ac` must
match the configuration, and the audio restarts at time zero, so check sync for
sources whose audio starts late. It is one process per stream and cannot switch
filters mid-stream.

**B. LV2 through `lv2`.** FFmpeg's `lv2` filter (`af_lv2.c`) sets only numeric
control ports and passes an empty event sequence. It has no way to send a file
path, restore plug-in state or compensate latency (`ladspa` has a `latency`
option but LADSPA has no file paths either). The filter would have to be fixed
in a generated per-filter bundle or selected by a number. That is clumsy for
FFmpeg alone, but an LV2 adapter for Ardour, Carla and other Linux hosts would get
FFmpeg use for free.

**C. Out-of-tree filter.** `libavfilter` is C11 and filters use internal,
unstable interfaces (`FFFilter`, `filters.h`), so the filter must be compiled
inside an FFmpeg source tree. The precedent is `rubberband`: `af_rubberband.c`
includes `rubberband/rubberband-c.h`, `configure` finds it with
`require_pkg_config` and adds `-lstdc++`, and `librubberband` sits in
`EXTERNAL_LIBRARY_GPL_LIST`. We would keep a patch series in this repository
against a pinned FFmpeg release and rebase it for each FFmpeg major release.

## Licensing

- FFmpeg is LGPL-2.1-or-later by default. `--enable-gpl` makes the build
  GPL-2.0-or-later and enables GPL-only parts; `--enable-version3` moves to
  (L)GPLv3. Convolver's GPL-2.0-or-later is compatible with both GPL modes.
- Put `libconvolver` in `EXTERNAL_LIBRARY_GPL_LIST` so `configure` refuses it
  without `--enable-gpl`. A distributed binary must come with corresponding
  source for FFmpeg, our patch, Convolver and its FFT library.
- FFTW is GPL-2.0-or-later (fine here). Ooura is permissively licensed and
  removes the FFTW dependency. libsndfile is LGPL-2.1-or-later. Any
  `--enable-nonfree` component makes the build non-redistributable, as FFmpeg
  already documents.
- The pipe (A) and an LV2 plug-in (B) are separate programs or modules, so they
  avoid linking questions for FFmpeg itself; the LV2 module stays GPL.
- Relicensing the portable core to LGPL would only matter for inclusion in
  LGPL-only FFmpeg builds. It is not needed for any option above.

## C API

FFmpeg needs a C header, and a C ABI is also what keeps the C++ library's lack
of a stable binary ABI from leaking into FFmpeg builds. Proposed
`include/convolver/convolver.h`, C99, with no exceptions crossing it:

```c
typedef struct convolver_filter convolver_filter;

typedef struct convolver_open_options {
    size_t struct_size;       /* sizeof(convolver_open_options), for growth */
    int sample_rate;          /* 0: any, otherwise list selection */
    size_t input_channels;    /* 0: any */
    size_t output_channels;   /* 0: any, like --output-channels */
    size_t block_frames;      /* 0: 256, as the CLI */
    size_t max_frames;        /* largest single process call */
    int fft_planning;         /* CONVOLVER_FFT_ESTIMATE ... _EXHAUSTIVE */
    const char *fft_wisdom;   /* optional wisdom file, UTF-8 */
} convolver_open_options;

/* Returns 0 or a negative CONVOLVER_E_* code. On failure, error receives the
   same explanatory text as the CLI (no-match details, missing IRs, rates). */
int convolver_open(const char *path_utf8, const convolver_open_options *opts,
                   convolver_filter **out, char *error, size_t error_size);
int convolver_replace(convolver_filter *, const char *path_utf8,
                      char *error, size_t error_size); /* crossfades */
void convolver_close(convolver_filter *);

size_t convolver_input_channels(const convolver_filter *);
size_t convolver_output_channels(const convolver_filter *);
uint32_t convolver_channel_mask(const convolver_filter *); /* WAVE bits */
size_t convolver_latency_frames(const convolver_filter *);
size_t convolver_tail_frames(const convolver_filter *);

/* Planar buffers; never allocates, locks or throws. */
int convolver_process_float(convolver_filter *, const float *const *in,
                            float *const *out, size_t frames);
int convolver_process_double(convolver_filter *, const double *const *in,
                             double *const *out, size_t frames);
void convolver_reset(convolver_filter *);
void convolver_set_bypass(convolver_filter *, int bypass);

/* For format negotiation before the stream rate is known. */
int convolver_probe(const char *path_utf8, convolver_probe_result *out,
                    char *error, size_t error_size);
const char *convolver_version(void); /* package version and FFT backend */
```

It wraps `load_configuration`, `inspect_configuration` (for `convolver_probe`)
and `HostProcessor`, so it reuses the crossfading replacement, bypass ramp,
nonfinite-input handling and allocation-free processing. Build it as a new
`Convolver::c` target, static and optionally shared with only the C symbols
exported, and install `convolver.pc` generated from `VERSION`. The header is
also usable by GStreamer or other C hosts.

## Filter design

`libavfilter/af_convolver.c`, an activate-style filter with one input and one
output.

| Option | Default | Meaning |
| --- | --- | --- |
| `filter` | required | IR sound file, `.cfg` or list. Relative IR paths resolve beside the `.cfg`, as everywhere else. |
| `out_channels` | 0 | Require this output count, like `--output-channels` |
| `block` | 256 | Partition size B. Larger is faster offline and only adds live latency. |
| `tail` | 1 | Append the filter tail after the last input, like the CLI |
| `keep_latency` | 0 | Keep the B-frame processing delay instead of trimming it |
| `gain` | 0 | Output gain in dB (later `no-clip`, using the existing estimate) |
| `match_rate` | 0 | Advertise the configuration's rates so FFmpeg resamples the *audio* to one |
| `fft_planning`, `fft_wisdom` | `estimate`, none | As the CLI's FFTW options |
| `dry`, `wet` | 0, 1 | Latency-aligned mix for reverb (FFMPEG-4) |

Behaviour:

- **Formats.** Accept and produce `fltp` and `dblp` (planar float/double; DSP is
  float, as in VST3). FFmpeg converts anything else automatically. Integer output
  and dither are left to FFmpeg's encoders and `aresample` dither options; our
  `Quantizer` is not used.
- **Channels.** Input must have the configuration's input count. The output
  layout comes from the `.cfg` speaker mask when it labels every output: the
  WAVE mask bits are the same as FFmpeg's native `AV_CH_*` bits for the 18
  standard positions. Otherwise the output has an unspecified-order layout with
  the right count. A mismatch between a labelled input layout and the mask logs
  a warning; channels are never reordered.
- **Rates.** By default accept any rate, select the list entry in
  `config_props`, and fail with an explanation listing available rates and the
  `aresample=RATE,convolver=...` fix. Never resample the IR. With `match_rate`,
  advertise only list rates so FFmpeg inserts the audio resampler itself.
- **Lists.** FFmpeg negotiates input and output formats separately, so a list
  whose matching entries have different output counts needs `out_channels`.
  `convolver_probe` supplies the candidates at init.
- **Timing.** The output has the same timestamps as the input: drop the first
  latency frames, and at end of input feed `latency + tail` frames of silence
  (only latency with `tail=0`). Split frames larger than `max_frames`.
- **Commands and timeline.** `process_command` `filter=PATH` calls
  `convolver_replace` (preparation blocks the graph thread for its duration;
  that is acceptable offline and must be documented for live use). Timeline
  `enable=` uses the bypass ramp via `AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL`.
- **Diagnostics.** Pass our messages through `av_log` at error level unchanged:
  users should not need the documentation to understand them. Log the Convolver
  version, FFT backend, latency and tail at verbose level.
- **Threads.** One engine per filter instance, no slice threading at first.
  FFmpeg uses its own FFT (`av_tx`), so FFTW's global planner is shared only
  with other FFTW users in the same process.

An `av_tx` FFT backend inside Convolver would remove FFTW from FFmpeg builds
entirely. Treat it as an [FFT-2](../../../TODO.md#fft-libraries-and-performance)
candidate with the usual oracle and benchmark evidence, not a requirement.

## Reverb use

Convolution reverb is IR convolution plus a dry/wet mix, so the engine already
covers the DSP:

- **Mono or stereo IR:** a sound-file filter with matching channels.
- **True stereo** (four IR channels L→L, L→R, R→L, R→R): a four-path `.cfg`,
  as used in the measurements above. `afir` cannot do this without splitting and
  mixing streams in the graph.
- **Pre-delay:** `.cfg` input or output delays.
- **Tail:** kept by default (`tail=1`); the reverb otherwise stops dead at the
  end of the input.
- **Mix:** missing today. Add a latency-aligned dry path and `dry`/`wet` gains in
  the host layer (the bypass already keeps a delayed dry signal), shared with the
  VST3 [VST3-4](../../../TODO.md#vst3-sdk-features-not-yet-used) parameters.
  Until then `asplit` and `amix` in the graph work when `keep_latency=0`.
- **Cost:** multi-second IRs are expensive at B=256 (see measurements); offline
  use should default to a larger block or wait for PERF-2.

## Testing

- **C API (this repo, CTest):** a C99 test compiled as C that includes the
  header with `-std=c99 -pedantic`; open/probe/process/replace/close; error
  text for no-match and missing files; allocation guard on process; output
  identical to `Engine` for the same configuration and block, and within the
  existing double-precision oracle tolerance (report the worst error).
- **Filter (CI job building FFmpeg):** fetch the pinned FFmpeg release, apply the
  patch, configure `--enable-gpl --enable-libconvolver` with a minimal component
  set, then run `ffmpeg` scripts that check:
  - output equals the CLI render of the same input (frames and samples), with and
    without `tail` and `keep_latency`;
  - variable frame sizes (`asetnsamples`), `fltp` and `dblp`, timestamps
    continuous and starting at the input's;
  - list selection by rate and channels, `out_channels`, and each documented
    error message;
  - `filter` command switching and timeline `enable=` (no clicks beyond the
    ramp, compared with the host-layer tests);
  - agreement with `afir` for a single mono IR over the input length, with its
    normalization off (`irnorm=-1` in master, `gtype=none` in 6.1): an
    independent implementation (5.5e-7 in the experiment above).
- **Drift:** a scheduled job builds the patch against FFmpeg git master and
  reports, without failing ordinary PRs, when internal interfaces change.
- Sanitizer builds of FFmpeg plus the filter for leaks and overruns at teardown.
- For upstream (D) only: a FATE test in `tests/fate/filter-audio.mak`, which
  needs FFmpeg's sample-suite conventions.

## Build and distribution

- **Convolver side:** `Convolver::c` target, `convolver.pc`, an Ooura or FFTW
  build choice, and a documented install prefix. The shared library avoids
  passing `-lstdc++` and FFTW through FFmpeg's link line.
- **FFmpeg side:** `ffmpeg/` directory in this repo with the patch series (new
  filter file, `configure`, `Makefile`, `allfilters.c`, `doc/filters.texi`),
  the pinned FFmpeg tag and checksum, and `scripts/build_ffmpeg.sh` that
  fetches, verifies, patches and configures. FFmpeg minor releases need a check;
  major releases usually need a rebase.
- **Binaries (optional):** GPL FFmpeg builds for Linux, macOS and Windows are a
  release-qualification task like [REL-1](../../../TODO.md#user-experience-and-distribution):
  source offer for everything linked, dependency closure, signing and clean
  machine tests. A Homebrew tap or vcpkg overlay port is lighter to maintain than
  binaries.
- **Versioning:** `configure` requires a matching Convolver minor version via
  pkg-config; the filter logs the exact Convolver build identifier.

## Milestones

1. **FFMPEG-1:** document the pipe workflow in the CLI guide (done in the PR
   adding this plan) and add a CI test that runs it when `ffmpeg` is installed.
2. **FFMPEG-2:** C API, `Convolver::c`, `convolver.pc`, C tests.
3. **FFMPEG-3:** out-of-tree filter patch, build script and CI job; filter tests
   above; user guide `docs/ffmpeg.md` when it works.
4. **FFMPEG-4:** dry/wet mix in the host layer, `gain`/`no-clip`, larger
   default block for offline use or PERF-2.
5. **FFMPEG-5:** decide whether to submit upstream, after FFMPEG-3 has users.
   Expect review questions about overlap with `afir`; the case rests on `.cfg`
   compatibility and routing.

## Decisions for the owner

- Whether to aim for upstream inclusion (D) or keep a maintained patch (C).
  Recommendation: keep C until it has users.
- Whether to publish FFmpeg binaries or only the patch and build script.
  Recommendation: script first; binaries only with the REL-1 evidence.
- Whether the C library should be shared by default. Recommendation: shared,
  with a static option.
