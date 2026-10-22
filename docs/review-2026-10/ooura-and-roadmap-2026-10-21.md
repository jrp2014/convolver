<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Ooura and documentation review: 21 October 2026

Reviewed `main` at `75f3924` (0.4.3). The documentation comparison starts at
`1c25493`, the version commit following the previous documentation refresh.
The background plug-in work was not modified or merged. This review adds
provenance and findings. A subsequent documentation-only correction, requested
on the same day, resolves the five findings in the plans and maintained guide;
see [resolution](#resolution). No proposed adapter was implemented.

## Ooura: no algorithm update required

The author's [FFT page](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html) still
advertises the 28 December 2006 packages. Downloaded both `fft.tgz` and
`fft2d.tgz` from that HTTPS site, read their contents without relying on file
dates, and calculated SHA-256 hashes. Their `fftsg.c` members are byte-identical.
The 1D package's `readme.txt` says the December 2006 correction was to `fftsg.f`,
the Fortran implementation. No newer C release was advertised there.

The [vendored README](../../portable/third_party/ooura/README.md#upstream-comparison-21-october-2026)
records the archive/member/local hashes. Comparison method:

1. Remove include directives for the textual comparison and preprocess each
   source with AppleClang `clang++ -E -P -x c++`, leaving optional Ooura thread
   modes disabled. This selects the code actually relevant to our backend.
2. Extract all function definitions, balancing braces. Normalize upstream
   `double` to `DLReal` and remove the block-scope function declarations that
   were relocated to namespace scope in the portable copy.
3. Compare identifier, literal and punctuation tokens, ignoring whitespace.
   All **36 definitions** match, with none added or removed. The historical
   `convolver/fft/fftsg.cpp` also matches the same upstream definitions.

This is not a claim of byte equality or identical double/single-precision
rounding. The local precision choice, namespace isolation, math-header imports,
prototype placement and removal of optional threading remain deliberate
adaptations. Neither historical source nor vendored transform code was changed.
Replacing the file would add churn and risk losing those adaptations without
bringing an upstream algorithm fix.

## Documentation findings

These findings describe the reviewed baseline. They are retained as the reason
for the corrections recorded below, not as claims that the current plans still
contain the same issues.

Navigation, concise opening summaries and separation of completed plans from
open proposals make the intended direction easier to follow. The new plans
have useful milestones and validation criteria. The following issues should be
resolved before treating their technical designs as implementation instructions.

### P2: input and output speaker layouts are being conflated

The [MFT media-type table](../plans/2026-10-21-media-foundation-transform.md#media-types-and-negotiation)
checks the input mask against the configuration's mask; the
[FFmpeg design](../plans/2026-10-21-ffmpeg-filter.md#filter-design)
warns about that same mismatch. The `.cfg` fourth header field labels **outputs**,
as documented in [configuration](../configuration.md#file-layout). It provides
no input speaker layout. Stereo input routed to 5.1 output legitimately has
different masks. The MFT rule would reject it; FFmpeg would emit a false warning.

Validate an output mask against the output count/layout. Input routing remains
index-based unless a separate input-layout contract is added. Do not infer an
input or output speaker arrangement solely from a count. Add stereo-to-5.1 and
same-count/different-arrangement cases to the planned negotiation tests.

### P2: the FFmpeg pipe example needs explicit stream and channel scope

The [maintained example](../portable-core.md#with-ffmpeg) and its copy in the
FFmpeg plan use `-map 0:a`, which selects every audio track. A raw PCM pipe
carries one audio stream; use `-map 0:a:0` for the example and explain how to
choose a different track. The two `-ac` values also have different jobs:
the decoder's must match configuration inputs, while the receiving process's
must match configuration outputs. A 2-in/6-out configuration needs 2 and 6,
respectively; reading six-channel samples as stereo corrupts channel/frame
interpretation. The existing command is a stereo-in/stereo-out example only.

Keep the existing timestamp-offset caveat, state which video/audio streams are
retained, and test multiple audio tracks plus unequal channel counts in
FFMPEG-1. See FFmpeg's [stream-selection rules](https://ffmpeg.org/ffmpeg.html#Stream-selection).
FFmpeg was not available on PATH here, so this finding is based on the format
contract and documented command semantics, not a fresh end-to-end pipe run.

### P2: specify the MFT buffer contract independently of callback capacity

The [allocation section](../plans/2026-10-21-media-foundation-transform.md#component-design)
derives `cbSize` from a supposed negotiated maximum input sample. That maximum
is not established by the listed media-type attributes. Microsoft's
[`MFT_OUTPUT_STREAM_INFO` contract](https://learn.microsoft.com/en-us/windows/win32/api/mftransform/ns-mftransform-mft_output_stream_info)
defines a minimum output buffer size and recommends audio block alignment for
uncompressed audio. `HostProcessor::max_frames` is an internal processing bound,
not a maximum IMFSample size negotiated with the host.

Define bounded input retention/queueing and partial output separately; process
large samples in chunks. Test an output buffer of one audio frame, repeated
partial outputs, back-pressure, empty/short inputs and drain. If the design
deliberately requires a larger minimum, document and test that requirement
instead of assuming a host-negotiated input maximum.

### P2: the proposed C API needs explicit host-layer prerequisites

The [C API options](../plans/2026-10-21-ffmpeg-filter.md#c-api)
offer FFT planning modes and wisdom, but the current
[`HostProcessor` constructor](../../include/convolver/host.hpp) accepts only
configuration, callback capacity and block size; it builds engines with default
FFT options. Importing wisdom alone would not forward a selected planning mode.
Either add/test that host-layer contract (including replacements) in FFMPEG-2,
or limit the initial API to estimate mode and reject unsupported options.

Likewise, the MFT plan should describe 256 as its chosen default, not a fixed
core property: `HostProcessor` accepts a block size. Query latency rather than
copying 256 into trimming/drain logic. The VST3 adapter itself does choose 256.

### P3: narrow the Equalizer APO coverage claim

The MFT recommendation says the export reaches every application. The
[upstream requirements](https://sourceforge.net/projects/equalizerapo/) exclude
applications bypassing system effects, including ASIO and WASAPI exclusive mode.
Describe supported endpoint/system-effects playback, not all Windows audio.
The export remains a useful smaller option when that coverage meets a user's
needs; its supported route/delay subset still needs explicit fixtures.

## Recommended order

The product direction is clear; the backlog needs one explicit execution order
so three adapter efforts do not become simultaneous prerequisites:

1. Continue existing **REL-1/HOST-1** VST3 distribution and named-host qualification
   in its separate task. Do not restart or merge that work as part of this review.
2. In this workstream, correct the plan contracts above, then deliver
   **DOC-1/FFMPEG-1**: redistributable examples and automated file/pipe checks.
   This benefits users without maintaining a patched FFmpeg distribution.
3. If demand justifies an integrated FFmpeg filter, settle channel negotiation,
   timestamps/tail policy, ownership/threading and FFT options before freezing
   **FFMPEG-2**'s C ABI. Then implement the pinned filter patch and equivalence
   tests (**FFMPEG-3**). Keep upstreaming/binary distribution as later decisions.
4. Keep **HOST-3** behind its existing Phase 0 user/host decision. For system
   effects, evaluate the bounded Equalizer APO export first; build MFT/DMO only
   for an identified host workflow. Do not make it a VST3 release dependency.
5. Keep new FFT backends and partitioning changes measurement-driven. **FFT-1 is
   complete**; Ooura freshness is not blocking any of these milestones.

## Resolution

The five documentation findings were addressed on 21 October 2026:

| Finding | Documentation correction |
| --- | --- |
| Input/output speaker masks | Both adapter plans use the `.cfg` mask only for outputs, keep inputs index-based and require unequal-layout negotiation tests. |
| FFmpeg pipe scope | The maintained guide and plan select `0:a:0` and `0:v:0`, explain omitted tracks, and distinguish input/output counts with a 2-in/6-out example. Existing timing caveats remain. |
| MFT buffering | The plan separates minimum output-buffer size from processing capacity, specifies single-sample retention and partial output, and adds small-buffer/back-pressure/drain test requirements. Latency comes from the processor. |
| C API FFT options | FFMPEG-2 now requires tested host-layer option propagation for both construction and replacement, off-thread preparation, backend rejection and queried latency before exposing the options. |
| Equalizer APO coverage | The MFT plan limits coverage to the system-effects path and explicitly excludes ASIO/WASAPI exclusive playback. |

[TODO's execution priorities](../../TODO.md#execution-priorities) now own the
current ordering. DOC-3 records the documentation work as complete, while the
adapter implementations and their tests remain open. The original numerical
measurements and review findings above are preserved. The revised FFmpeg command
has not been rerun here; its automated checks remain FFMPEG-1.

## Validation

Fresh builds on Apple Silicon/macOS with AppleClang 21, Debug, ASan/UBSan and
real libsndfile 1.2.2, against source `75f3924`:

| Check | Result |
| --- | --- |
| Ooura full CTest, `build/ooura-review` | 28/28 passed |
| FFTW 3.3.11 NEON full CTest, `build/presets/debug-sanitizers` | 28/28 passed |
| Ooura native engine versus direct double oracle | Worst absolute error `2.94604e-07` |
| FFTW native engine versus direct double oracle | Worst absolute error `5.26131e-07` |
| `backend_equivalence.py` with those two CLIs | 16 cases, 213,324 samples passed; maximum absolute difference `6.556510925292969e-07`, RMS `7.354949969747637e-08` |

The full suites include the independent DFT checks, larger transform/round-trip
cases, engine and host tests, real-file/corpus checks and external CMake consumer.
No DSP source was changed. These are correctness checks, not performance results.

The supplied documentation passed the repository's lint and maintained-link
checks before this review. After the corrections, ShellCheck, markdownlint,
strict YAML, actionlint and the local-link checker with its regression tests
passed. The new review page was supplied explicitly to pre-commit. An additional
link/anchor check covered all six changed pages, including plans and provenance
that the normal maintained-page scan excludes.

These tests do not validate the proposed FFmpeg/MFT adapters, Windows playback,
or production plug-in packaging. Existing numerical benchmark claims in the
new plans were not rerun and remain evidence scoped to their recorded setup.
