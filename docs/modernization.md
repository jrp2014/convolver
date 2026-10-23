<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Modernization direction

[Documentation index](README.md) · [Implementation map](develop/architecture.md) · [TODO](../TODO.md)

Convolver already has a portable C++20 core, CLI, VST3 adapter, three FFT backends,
configuration inspection, integer dither/noise shaping, CMake exports and tests
against real libraries. These are the starting point, not proposals for a port.
The [current review](../REVIEW.md) links each area to its implementation.

## Priorities

1. Finish a dependable product: someone unfamiliar with the repository can
   download, install and use an exact preview package by following the
   documentation (the next milestone in [release readiness](release-readiness.md)).
   Make the plug-ins straightforward to build, install and use in named hosts,
   with reproducible packages and documented platform support.
2. Preserve configuration and stream behavior. Record consumer-specific
   differences and test changes against independent references, and against
   files kept from released builds.
3. Improve speed only for measured workloads, keeping latency, memory and callback
   behavior visible alongside throughput. Resource budgets come first.
4. Keep maintenance cost down: move code into shared layers only when two
   formats need the same behaviour (as `HostFilter` was), and do not generalise
   host lifecycle or release machinery beyond the requirements in hand. The
   architecture is sound enough; the next decisions matter more than another
   substantial refactor (jrp's
   [guidance](review-2026-10/release-readiness-guidance-2026-10-22.md),
   22 October 2026).

[TODO.md](../TODO.md) is the single list of outstanding work and candidate
libraries. This page explains direction; dated implementation plans describe
particular projects. Check the source and open pull requests before resuming a
plan. The separate plug-in distribution branch is merged into `main`, except
its signing scripts; its VST3 release pipeline is the one that PLUG-5 extends
to LV2 and AU, as the [plug-in formats plan](plans/2026-10-21-plugin-formats.md)
describes.

## Dependency choices

FFTW remains the default FFT backend unless the build includes a plug-in
(`CONVOLVER_BUILD_VST3=ON`). Then `AUTO` picks the built-in Ooura FFT on Linux
and Windows and Accelerate on macOS for the whole build, CLI included, because
other code in the host process may call the same FFTW, whose planner Convolver
cannot lock ([plug-in builds](develop/fft-backends.md#plug-in-builds)). Build the CLI
separately to keep FFTW for offline rendering. Accelerate uses an Apple system
framework; Ooura is built in and avoids a separate FFT dependency. Use
[the FFT guide](develop/fft-backends.md) for contract details and dated measurements;
none is asserted to be fastest for every workload.

[libsndfile](https://libsndfile.github.io/libsndfile/) remains the file-decoding and
encoding boundary. The core and host libraries can omit it. It is neither an
audio-device layer nor a resampler. Replacing it would require a concrete format,
packaging or correctness benefit and corresponding real-file tests.

Keep standard C++20 facilities where suitable and platform-specific operations
at the I/O, FFT or host boundary. A new library or language standard should earn
its deployment cost. Alternative FFT implementations are investigations in
[TODO](../TODO.md#fft-libraries-and-performance), not implicit fallbacks.

## What belongs in history

The [history guide](history.md) holds the SourceForge recovery and downstream
lineage. Historical applications remain preserved; their constraints should not
become requirements for the portable build. Older plans and measurements retain
their original scope and dates.

The [plug-in distribution guide](plugins/index.md) describes pinned, unsigned
preview packaging and source builds. Signing, clean installation and named-host
qualification remain gates in the [release record](releases/developer-preview.md).
