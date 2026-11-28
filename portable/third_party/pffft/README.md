# PFFFT

PFFFT ("a Pretty Fast FFT") is the FFT of the `PFFFT` backend
(`portable/fft_pffft.cpp`), which `AUTO` chooses for plug-in builds on Linux
and Windows (FFT-2). It is Julien Pommier's SIMD version of FFTPACK, from the
[maintained fork](https://github.com/marton78/pffft) by Marton Danoczy and
Hayati Ayguen.

| | |
| --- | --- |
| Release | v1.3.1, 20 November 2026 |
| Commit | `fbe9c2b96530ce274f12e008336a8202e15b859b` |
| Copied | 28 November 2026 |
| Licence | FFTPACK's NCAR licence and the authors' BSD-style notices: permissive, compatible with GPL-2.0-or-later |

## What is here

Only what the single-precision transform needs, byte for byte from the
release: `LICENSE.txt`, `include/pffft/pffft.h`, `src/pffft.c`,
`src/pffft_common.c`, `src/pffft_priv_impl.h` and the float SIMD headers in
`src/simd/` (`pf_float.h` includes every one of them; the compiler keeps the
one for its target). The double-precision transform, the fast convolution,
the C++ wrapper, the mixers, tests and benchmarks are left out.
`NOTICE.txt` is ours: `LICENSE.txt` and the other copyright notices in the
files compiled, which every binary package ships as `licenses/PFFFT.txt`.

No file is changed. It is third-party code: not reformatted, linted or
analysed by clang-tidy (`.clang-format-ignore`, `scripts/check_format.py`).

## How it is built

As PFFFT's own CMake build does by default (`CMakeLists.txt`, the `PFFFT`
branch of the FFT backend choice): C, static (`PFFFT_STATIC_DEFINE`), hidden
symbols, `_USE_MATH_DEFINES`, without the project's warnings. SSE on x86-64,
which every x86-64 processor has, NEON on 64-bit ARM (Apple silicon, Linux
and Windows on ARM), portable scalar code on any other target. No FMA on
x86-64: compiled with `-mfma` or `/arch:AVX2` its butterflies would fuse,
which measured no faster end to end and would need a second copy chosen at
run time. The NEON code on 64-bit ARM fuses multiply-adds (`vfmaq_f32` in
`pf_neon_float.h`) whatever the flags, as upstream ships it ([evaluation](../../../docs/develop/fft-backends.md#pffft-on-linux-and-windows)).
`convolver --version` names the instructions used, for example
`FFT backend: PFFFT / 1.3.1, SSE1`.

## How Convolver uses it

`portable/fft_pffft.cpp` uses only the ordered real transforms
(`pffft_transform_ordered`), with PFFFT's aligned buffers and a work buffer
of its own for each transform, made when the engine is prepared. A prepared
setup is read-only and the library keeps no mutable global state, so any
number of instances, and other plug-ins in the same process, run side by
side. A transform given its work buffer allocates nothing.

PFFFT's SIMD real transform takes lengths that are a multiple of 32 with no
prime factors but 2, 3 and 5. The engine's transforms are powers of two from
128 points; shorter ones, which only the tests' tiny partition layouts ask
for, use the vendored Ooura transform (`portable/fft_ooura_transform.hpp`).
`tests/native/fft_tests.cpp` checks both sides of that boundary against a
direct DFT.

## Updating

Copy the same files from the new release, update the table above, the
version in `fft_backend_version()` and the release and commit in `NOTICE.txt`'s first
paragraph, and add any new copyright notice in the compiled files to
`NOTICE.txt`. Then run the full test suite with
`-DCONVOLVER_FFT_BACKEND=PFFFT` and the oracle tests (`native.fft`,
`native.engine`), and check the release's changes for new allocation or
global state in the transform: the C++ allocation guard in `native.engine`
does not see C's `malloc` or a stack array, so this check rests on reading
the source (at v1.3.1 only `pffft_new_setup` and `pffft_aligned_malloc`
allocate, and a transform given a work buffer keeps a one-element stack
array).
