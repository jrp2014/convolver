<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# FFT-2

## Changed

- Plug-ins on Linux and Windows (FFT-2): the FFT is now PFFFT, built in,
  instead of Ooura. Each instance needs 19–38% less CPU on a Linux x86-64
  machine (a stereo room-correction filter at 48 kHz falls from about 0.85%
  to 0.54% of a core), the worst callbacks are about half as long, and
  filters load faster (30 s stereo, about 40% less preparation). Output
  changes by float rounding only, under 7e-7. macOS keeps Accelerate, and
  the command-line tools keep FFTW where they used it. `convolver --version`
  and the plug-ins' log name it, with the instructions it uses: `PFFFT /
  1.3.1, SSE1`. Packages ship its licence as `licenses/PFFFT.txt`. See
  [PFFFT on Linux and Windows](../docs/develop/fft-backends.md#pffft-on-linux-and-windows).

## For contributors

- FFT (FFT-2): `-DCONVOLVER_FFT_BACKEND=PFFFT` builds PFFFT v1.3.1,
  vendored in `portable/third_party/pffft/` with its notices (`NOTICE.txt`,
  installed as `licenses/PFFFT.txt` in packages and in
  `share/doc/Convolver/licenses/` by `cmake --install`) and one local
  change: `pffft_new_setup` returns `NULL` when an allocation fails, where
  upstream writes through it, so running out of memory while a filter is
  prepared throws `std::bad_alloc` instead of crashing. Behind `RealFft` in
  `portable/fft_pffft.cpp`: ordered real transforms, SSE without FMA on
  x86-64, NEON on 64-bit ARM (which fuses multiply-adds, as upstream ships
  it), hidden symbols, transforms under 32 points through
  Ooura. `AUTO` chooses it for plug-in builds on Linux and Windows, and the
  release policy for the Linux and Windows candidates. `FFTW`, `ACCELERATE`
  and `OOURA` stay selectable; Ooura's transform moved into
  `portable/fft_ooura_transform.hpp`, shared by both built-in backends, with
  bit-identical output. A PFFFT build enables C and takes the C compiler
  beside the C++ one unless one is named. New `pffft` CMake presets. See
  [FFT backend](../docs/develop/build.md#fft-backend).
- Tests and CI (FFT-2): every native Linux and macOS job builds and tests
  PFFFT and compares its CLI output with FFTW's, and clang-tidy checks the
  built-in backends' adapters; `native.fft` checks 32 points, PFFFT's
  smallest, and 131,072; `vst3.fft_symbols` and `lv2.fft_symbols` refuse an
  exported PFFFT symbol; `package_smoke.py` and `verify_artifact.py` require
  `licenses/PFFFT.txt` and `licenses/Ooura.txt` in a PFFFT package, and
  the consumer install test the same files in an installation;
  `native.pffft_allocation` fails each of PFFFT's allocations in turn;
  release candidates keep debugging symbols for PFFFT's C code, record its
  C flags and refuse nonportable ones there too; both sanitizer jobs
  instrument it; the workload job measures PFFFT on Linux and Windows. Measurements:
  [PFFFT, Ooura and FFTW on Linux](../docs/develop/fft-measurements.md#pffft-ooura-and-fftw-on-linux).

## Done

- [x] **FFT-2 — PFFFT as the Linux and Windows plug-ins' FFT.** 28 November
  2026, after measurements on Linux x86-64: 19–38% less CPU per instance
  than Ooura, with the same oracle accuracy; Ooura, FFTW and Accelerate stay
  selectable, and FFT-6 compares PFFFT with Accelerate on the Mac
  ([PFFFT](../docs/develop/fft-backends.md#pffft-on-linux-and-windows)).

## Decisions: Licences and dependencies

- **28 November 2026.** PFFFT (the maintained fork, v1.3.1, BSD-style
  FFTPACK licence, with one local fix so that a failed allocation in its
  setup returns `NULL` instead of crashing) is a shipped dependency: the Linux and Windows plug-ins'
  FFT in place of Ooura, compiled with SSE and without FMA on x86-64 (NEON,
  where it is built, fuses multiply-adds as upstream ships it), using
  its ordered transforms and not its convolution routine. Ooura, FFTW and
  Accelerate stay buildable and tested; macOS keeps Accelerate unless a
  paired comparison says otherwise (john: "Add PFFFT as the default. Keep
  other options and variants buildable and documented. Make sure that the
  packaging works"). See
  [PFFFT on Linux and Windows](../docs/develop/fft-backends.md#pffft-on-linux-and-windows).
