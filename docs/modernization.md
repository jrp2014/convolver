# Modernizing Convolver

Assessment: 17 October 2026, starting from `main` at `b3b479f`.
This is a proposed architecture and delivery order, not a claim that the native
port or alternative FFT backends already exist. The accompanying CMake project
currently runs the portable regression harnesses only.

## Recommended direction

Build a small portable C++ engine, a reliable offline renderer and a streaming
CLI first. Preserve the existing configuration semantics. Use CMake, keep FFTW
as the initial reference backend, retain libsndfile for files, and add Apple
Accelerate/vDSP as the first independent FFT implementation. Benchmark PFFFT
before deciding whether it becomes the compact default on other platforms.

This produces something useful before taking on another plug-in UI or device
manager: a reference implementation of the Convolver format with reproducible
output, current builds and explicit latency/tail behaviour.

| Direction | Value | Recommendation |
| --- | --- | --- |
| Portable library plus CLI | Embedding, offline verification, pipes, reusable routing and filters | First milestone; best balance of usefulness and maintenance |
| VST3/CLAP plug-in | Modern replacement for ConvolverVST | Second product milestone after the core and lifecycle tests |
| Interoperability tools | Config validation, reference renders, conversion fixtures | Develop alongside the core; benefits existing users immediately |
| Standalone system DSP application | Device selection, clock handling, routing UI | Only after identifying a need that existing applications do not meet |
| Preservation only | Historical source, documentation and regression vectors | A viable stopping point if maintaining a product is undesirable |

CamillaDSP already offers a cross-platform FIR/IIR engine, mixing and device
backends. Recreating that complete application is a much larger commitment than
maintaining a focused library and reference renderer. [CamillaDSP upstream](https://github.com/HEnquist/camilladsp)

## Separate the core from the hosts

The current `convolver/convolution/config.h` imports Windows/ATL and globally
selects FFTW and libsndfile. `convolution.cpp` mixes scheduling, backend calls and
per-frame I/O. The dormant Ooura branches include stale syntax and accessors;
changing a preprocessor definition is not a working multi-backend design.

Use C++20 for the new core, including ordinary RAII ownership, fixed-width
external values, checked sizes and spans for audio buffers. This is a proposed
new-core baseline; the historical sources and extraction tests need not be
mechanically converted at once. Keep these responsibilities separate:

- **Configuration:** parse the historical format into a validated routing graph;
  retain a distinct legacy importer if a new, versioned format is introduced.
- **Preparation:** load IRs, validate sample rates, select block sizes, prepare
  FFTs, allocate buffers and construct immutable filter data off the audio thread.
- **Processing:** accept planar float blocks, with a double option if justified;
  adapt interleaved/PCM data at the boundary. Support arbitrary caller frame
  counts through a preallocated staging buffer.
- **Lifecycle:** explicit reset, latency, finite tail support and drain/end of
  input. Separate true IR length, partition payload length, FFT length and padded
  storage length. These quantities are currently too easy to confuse.
- **Hosts:** file/pipe tools, plug-ins and any device application use the same
  processing contract; platform headers stay in adapters.

Keep uniform partitioning as the first implementation. For a conventional
partition payload of B frames, use a 2B real FFT and preserve the existing
leading B-frame processing latency until a separately tested mode changes it.
Expose latency in samples/milliseconds rather than making users guess from a
partition count. Filter group delay, routing delay and device buffering remain
separate from algorithmic latency.

An offline render should have a documented latency-trimming option and include
the full FIR tail. For a nonempty single route, after trimming algorithmic
latency, the length is input frames + IR frames - 1 + route delay. A matrix uses
the maximum support of connected routes. The recovered Pipe deliberately keeps
its leading latency; preserve that compatibility behaviour. Fix the older CMD
EOF handling through the shared drain API instead of copying its fixed-block
padding logic.

## FFT library choices

| Backend | Best role | Tradeoffs and constraints |
| --- | --- | --- |
| **FFTW 3** | Portable baseline; Linux/Windows and comparison on macOS | Flexible sizes and mature planning. Plan outside processing, manage alignment and precision explicitly. Use `fftw3f` for float, `fftw3` for double. |
| **Apple Accelerate/vDSP** | First alternative on macOS/Apple Silicon | System framework, no separately shipped FFT library. Different real-spectrum packing and scaling; supported lengths depend on the selected FFT/DFT API. Benchmark against FFTW. |
| **PFFFT, maintained marton78 fork** | Compact SIMD backend for ARM/x86; candidate default where deployment simplicity matters | SSE/AVX/NEON support and float/double build options. Restricted supported sizes and alignment requirements. Native unordered spectra can avoid reorder passes. |
| **Intel oneMKL CPU DFTI** | Optional backend for Intel-focused deployments | Worth adding only after a measured gain justifies another dependency. Control threading; use the CPU interface without adding SYCL solely for convolution. Current platform support makes it a Windows/Linux option, not the Mac default. |
| **PocketFFT** | Portable comparison/fallback, especially offline work | C++ header-only, BSD-3-Clause, broad lengths and precisions. Audit allocation and plan-cache behaviour before considering callback use. No reason to ship every backend initially. |

FFTW is still maintained: 3.3.11 was released on 21 April 2026, adding SVE
support and Apple ARM cycle counters among other changes. The old bundled
header/import arrangements should be replaced by a matching installed package.
[FFTW release notes](https://fftw.org/release-notes.html)

Backend references: [Apple FFT setup](https://developer.apple.com/documentation/accelerate/vdsp_create_fftsetup),
[Apple packing/scaling guide](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html),
[PFFFT options](https://github.com/marton78/pffft/blob/master/README.md),
[PFFFT size/layout API](https://github.com/marton78/pffft/blob/master/include/pffft/pffft.h),
[oneMKL supported platforms](https://www.intel.com/content/www/us/en/developer/articles/release-notes/onemkl/2026.html),
[PocketFFT interface](https://github.com/mreineck/pocketfft/blob/cpp/README.md).

FFTW uses the GPL and libsndfile the LGPL; changing FFT libraries does not
relicense Convolver itself. Preserve dependency notices and select the exact
dependency versions before packaging.
[FFTW distribution licence](https://fftw.org/download.html),
[libsndfile licence](https://libsndfile.github.io/libsndfile/)

### The abstraction matters more than the selection switch

Define a small prepared backend interface: supported lengths, aligned buffer and
scratch requirements, forward real transform, inverse real transform, and
spectral multiply/accumulate. Choose a backend at construction, not per sample.
One dispatch per block is sufficient; avoid virtual calls inside the bin loop.

Start with a documented logical half-spectrum of N/2+1 complex bins for even N,
an unscaled forward transform and a 1/N-normalized inverse. Tests should establish
both `inverse(forward(x)) = x` and unit-gain convolution. The current engine
pre-scales filters for an unnormalized inverse; remove that convention only in
the new adapter path, with tests that detect double normalization. Real vDSP
packing and scale factors must be converted deliberately, including DC and
Nyquist. Do not reinterpret every library's packed array as FFTW complex data.

Once correct, allow opaque backend-native spectra and a backend-owned MAC to
avoid repacking every block. PFFFT explicitly provides multiplication on its
unordered representation. Keep canonical export only for diagnostics/tests.
Do not pass native spectra between different backends. This preserves the
performance option without leaking FFT layouts into routing or file parsing.

Explicit selection should fail clearly if unavailable. An `AUTO` policy can
choose a documented platform default, but print the selected backend and build
features for reproducibility. Unsupported sizes must be rejected or deliberately
padded during preparation, with reported latency; never silently change them
during processing. Initial shared benchmarks can use supported powers of two,
then assess mixed-radix sizes separately.

FFTW execution is thread-safe with independent data; planning and destruction
need coordination. Keep plan creation, wisdom import/export and teardown off
callbacks, and account for multiple plug-in instances. Key any saved wisdom by
library/build, precision, CPU capabilities, sizes, flags and threading policy.
[FFTW threading rules](https://ftp.fftw.org/doc/Thread-safety.html)

## Keep libsndfile, modernize its integration

**Yes: libsndfile remains a good fit for IR loading and offline audio files.**
Its current documented release is 1.2.2; it supports the WAV/AIFF/FLAC/RAW formats
needed here and has a maintained CMake integration. MP3 support was added in
1.1.0, although lossy formats are unnecessary for impulse responses.
[Project and release history](https://libsndfile.github.io/libsndfile/),
[format support](https://libsndfile.github.io/libsndfile/formats.html)

Use a packaged supported release, matching its headers and library, rather than
resurrecting the 1.0.x DLLs from the archive. Prefer
`find_package(SndFile CONFIG REQUIRED)` and the `SndFile::sndfile` imported target;
offer a pkg-config fallback where distributions do not provide the CMake config.
Keep optional codec dependencies explicit and query actual runtime format
support instead of treating the recovered historical format table as exhaustive.
[Upstream CMake configuration](https://github.com/libsndfile/libsndfile/blob/master/CMakeLists.txt)

Keep disk access and decoding outside the audio callback. Test real files and
real descriptors, not just mocked calls: float WAV, PCM16/24/32, multichannel
ordering, RAW endianness, short reads, truncated files, close errors and sizes
beyond 32-bit counters. Preserve descriptor ownership. A pipe is not a seekable
file: offer an explicit RAW float protocol with rate/channel metadata, and reject
output formats requiring seeks/header rewriting when the destination cannot
support them. Do not promise every libsndfile format works on stdin/stdout.

libsndfile is neither the device layer nor the resampler. Keep sample-rate
mismatch rejection as the default. An optional offline IR-resampling step can
use libsamplerate, with explicitly tested gain, timing and output length; it
must not silently reinterpret coefficients at a different sample rate.
[libsamplerate](https://libsndfile.github.io/libsamplerate/)

A WAV-only reader could reduce dependencies for a deliberately minimal embedded
target, but would discard useful existing file support. A general media decoder
adds complexity without helping the initial room-correction workflow. Neither
is a compelling default replacement for libsndfile.

## Performance work, in priority order

These are code-based opportunities, not measured speedup claims. There is no
current end-to-end native performance baseline.

1. **Measure release builds.** Record processing time per callback (median,
   p95, p99 and maximum observed), deadline misses, memory, setup time and
   numerical error. Separate real-time and offline throughput. At 48 kHz a
   64-frame callback has only 1.333 ms for the entire host processing chain.
   A maximum observed time is not a hard real-time guarantee.
2. **Use blocks and reuse transforms.** The current partitioned loop performs
   forward and inverse transforms inside each path. Group identical input mixes
   including their delays/weights and transform them once per block. Where
   lengths, schedules and delays permit, sum routed spectra before the inverse
   transform to approach one inverse per output. Preserve sparse routing;
   expanding every path into a dense channel matrix can cost more memory/work.
3. **Improve memory access and MACs.** Contiguous aligned spectra, cache-friendly
   partition order, preallocated scratch, and SIMD complex multiply/accumulate
   are the next candidates. Compare input-spectrum history (frequency-domain
   delay lines) with the current future-output accumulation scheme. Neither is
   automatically better for every routing graph.
4. **Tune partition scheduling.** First sweep uniform B values. Later add a
   small head and larger tail partitions, or a direct FIR head, for long IRs
   with low latency. Validate every transition and spread tail work across
   callbacks; a good average with periodic deadline spikes is a regression.
5. **Control real-time work.** No allocation, plan creation, file I/O, logging,
   blocking locks or final object destruction in processing. Build replacement
   filters on a worker, warm their state, align latency, crossfade and retire
   the old state off-thread. Test denormals and the chosen floating-point mode.
6. **Use threads selectively.** Start with single-thread transforms. For large
   jobs compare parallel independent channels/tail groups against threaded FFTs;
   avoid nested thread pools. GPU offload is a later offline/large-batch option,
   not the starting point for small low-latency callbacks.

Benchmark 44.1/48/96/192 kHz, 32–1024-frame caller blocks, short crossover through
multi-second IRs, stereo, 2-to-8 crossover, 8-channel diagonal and dense routing.
Run on Apple Silicon and x86-64, with an ARM Linux target if desired. Include
silence, long decays and filter transitions. Publish CPU/compiler/backend,
precision, thread count, routing and latency alongside results. Shared CI timing
is useful for gross regressions; hardware-specific performance decisions need a
controlled runner.

Keep float as the compatibility baseline and compare float MACs with double
accumulation/full double for long filters and cancellation-heavy matrices.
The earlier three-multiply complex shortcut already lost small components:
operation count alone is not an accuracy or speed argument. Avoid blanket
`-ffast-math` and machine-specific instructions in distributable binaries;
evaluate FMA and CPU dispatch with the numerical suite enabled.

## CMake and tests

The repository now has a **regression-only** CMake/CTest entry point and a
macOS/Linux CI workflow. It requires Clang/AppleClang, Python and working address
and undefined-behaviour sanitizers. Individual tests compile their extracted
C++ harnesses when run. There is no native application/library target yet.
See [testing instructions and coverage](testing.md).

For the native port, use target-scoped C++ requirements, includes and dependency
links. Suggested targets are `convolver_core`, `convolver_io`, `convolver_cli`
and separate optional host adapters. The core should also be testable with
in-memory coefficients and no file or plug-in dependency. Avoid one global
`config.h` that enables every platform and dependency.

Use CMake presets for repeatable configurations. Find installed dependencies
first; optional fetching must use pinned tags/commits and checksums, not a
moving branch or unconditional network access at configure time. A pinned
vcpkg manifest is a reasonable Windows CI dependency strategy; distribution
packages are useful on Linux and macOS. Report the versions actually linked.
Install/export the library only after its public API settles.
[CMake preset reference](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)

Required release gates are broader than the existing extraction tests:

| Layer | Required evidence |
| --- | --- |
| FFT adapter | Real libraries: impulses at every offset, DC, Nyquist, tones/random data, scaling, all supported lengths, alignment, in-place/destructive behaviour, float/double, invalid length rejection |
| Native convolution | Independent double direct-convolution oracle for short cases; long randomized/metamorphic cases; awkward IR lengths; chunk invariance; matrix routing, gains/delays, cancellation, reset, silence, exact tail |
| Files and pipes | Real libsndfile codecs and seekable/non-seekable descriptors; PCM endpoints including full-width PCM32; empty/truncated/corrupt input, sample-rate mismatch, precise frame counts and failures |
| Configuration | Archived examples adapted only for fixture paths; negative zero and zero-as-unity semantics; malformed/oversized input; clear diagnostics; fuzz parser/IR boundaries with bounded resources |
| Hosts | Actual native builds; bus negotiation, float/double, in-place/additive output, disconnect/reconnect, bypass, state reload, seeks, latency/tail reporting and filter changes |
| Real-time behaviour | Allocation/lock instrumentation, bounded processing, long silence/denormals, independent instances, transition stress, separate race checks where supported |
| Packaging/docs | Clean install and sample invocation; dependency/version report; generated examples match CLI; historical manifest integrity and local links |

Use combined absolute/relative error bounds and an energy/error measure, with
explicit handling near zero and rejection of nonfinite results. Derive tolerances
from precision, length and reference error; do not weaken them to make a new
backend pass. Cross-backend agreement alone can share a scaling mistake, so
retain the independent time-domain oracle. Run both optimized and debug builds,
with sanitizers separately from performance measurements.

The eventual native matrix should include Linux GCC/Clang, macOS AppleClang
ARM64 (plus Intel where supported), and Windows MSVC x64, adding ARM64 when it
is a supported product target. Exercise each enabled FFT backend and real file
I/O. The initial workflow does not yet establish this coverage.

## Documentation and downstream compatibility

All 18 historical SourceForge pages and their required local assets/examples
are preserved: 99 original files, with archive hashes. The recorded live-site
comparison found matching visible text, hyperlink/image references and
preformatted examples on every page. Keep this archival copy unchanged and put
current build/API/CLI instructions alongside it. Do not describe the selectively
recovered source tree as a complete stable 4.5 release.
[Historical documentation](historical/convolver-4.4/README.md),
[live-site comparison](review-2026-10/sourceforge-documentation-check.json)

The new offline documentation test checks bytes, inventory and local references
without requiring the owner's archive or network access. External sites may
disappear; their availability should not make ordinary CI fail.

Preserving the format has value beyond this implementation:

- **Album Player:** its manual credits ConvolverVST as the prototype for its
  convolver. Recovering the maintained adaptation's source could avoid repeating
  portability work; its fixes cannot be inferred from that credit alone.
  [Album Player manual, section 6.3](https://albumplayer.ru/linux/album_player_linux_manual_ru.pdf)
- **Audiolense/JRiver:** Audiolense's author credits John Pavel's configuration
  protocol and describes arranging JRiver support. This establishes format
  adoption, not shared DSP code.
  [Author's account](https://www.avnirvana.com/threads/al-filter-wav-file-inquiry.13436/)
- **Roon:** documents Convolver configuration support but requires relative IR
  paths and does not accept configuration-list files. Keep consumer-specific
  compatibility profiles instead of silently narrowing Convolver's own format.
  [Roon documentation](https://help.roonlabs.com/portal/en/kb/articles/dsp-engine-convolution)
- **CamillaGUI:** the earlier source audit identified an existing importer that
  translates Convolver routing, delays and FIR references into CamillaDSP
  configuration. Compare and extend compatibility fixtures around that work
  before writing a competing converter.
  [Audited importer revision](https://github.com/HEnquist/camillagui-backend/blob/3c753054ca2a8225476712f917e6ab4124eb4888/backend/convolver_config_import.py)
- **Other implementations:** configuration-compatible products such as Hang
  Loose Convolver are interoperability targets, not proof of code ancestry.
  [HLC specification](https://accuratesound.ca/hlc-plugin/)

Publish small redistributable configurations, IRs and reference outputs as a
compatibility corpus. Version new syntax and retain a legacy reader. A config
validator/converter is a useful deliverable even before a modern plug-in.

For plug-ins, VST3 offers the first replacement path for the old VST wrapper;
CLAP is another lightweight option according to host demand. Both SDKs now use
MIT licences. That does not change this repository's GPL-2.0-or-later notices
or other dependency terms. Keep old VST2/DirectShow/WMP adapters isolated as
legacy components rather than making them dependencies of the portable build.
[VST3 SDK](https://github.com/steinbergmedia/vst3sdk),
[CLAP licence](https://github.com/free-audio/clap/blob/main/LICENSE)

## Delivery order and acceptance criteria

1. **Preserve the baseline:** CTest, sanitizer CI, documentation integrity and
   explicit coverage gaps. This change supplies that foundation.
2. **Native core + FFTW + libsndfile CLI:** build unmodified production translation
   units on all three desktop platforms; pass direct-convolution and real-file
   tests; finish the remaining PCM32, CMD EOF and host-boundary work within the
   components being shipped. Legacy hosts can remain unsupported explicitly.
3. **vDSP adapter + benchmark runner:** same public API and fixtures, normalized
   output, clear backend selection and published end-to-end measurements.
4. **Measured optimizations:** transform reuse and block I/O first, then PFFFT
   and non-uniform partitioning as the results justify. Each change must retain
   accuracy and improve the intended latency/CPU/memory workload.
5. **Modern plug-in:** reuse the verified core, add host validation, state,
   filter switching and packaging. Choose device/UI expansion from actual use.
