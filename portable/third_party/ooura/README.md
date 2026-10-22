# Ooura FFT

`fftsg.cpp` is Takuya Ooura's general-purpose FFT package (`fftsg.c`, split-radix,
1996–2001), used by the optional `OOURA` FFT backend (`portable/fft_ooura.cpp`)
for builds without an FFT library dependency.

Licence (from the package): "You may use, copy, modify and distribute this code
for any purpose (include commercial use) and without fee. Please refer to this
package when you modify this code." This permissive licence is compatible with
Convolver's GPL-2.0-or-later. Upstream: <https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html>.

The copy is taken from the historical engine's `convolver/fft/fftsg.cpp` (now
`historical/fft/fftsg.cpp`; the version distributed with DRC, which names the
sample type `DLReal`). Changes, listed at the top of the file: single precision,
a private C++ namespace, thread support removed, `<cmath>` included outside the
namespace, and namespace-scope prototypes in place of block-scope ones (MSVC
binds those to the global namespace). The transform code is otherwise unchanged;
it is third-party code and is not reformatted or linted.
`tests/native/fft_tests.cpp` checks the backend against a direct DFT.

## Upstream comparison: 21 October 2026

Checked against the official `fft.tgz` and `fft2d.tgz` downloads. The upstream
site labels both archives 28 December 2006; `fft/readme.txt` identifies that
release's correction as a Fortran `fftsg.f` fix. Both archives contain the same
`fftsg.c`. This is the latest release advertised by the author's site at the
time of the check, not a claim about every third-party fork.

| File | SHA-256 |
| --- | --- |
| [fft.tgz](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.tgz) | `52bb637c70b971958ec79c9c8752b1df5ff0218a4db4510e60826e0cb79b5296` |
| [fft2d.tgz](https://www.kurims.kyoto-u.ac.jp/~ooura/fft2d.tgz) | `ada7e99087c4ed477bfdf11413f2ba8db8a840ba9bbf8ac94f4f3972e2a7cec9` |
| `fftsg.c` in either archive | `21f8ea961f13284b0272798af99c5ad6081cd30f339c5d4bcf82c05b19fbdc2a` |
| Our `fftsg.cpp` at `75f3924` | `6a779035fe3d99917f8de269f1a2706f27f725a9f3aa9b549017c5e5d92f52a2` |

All 36 non-threaded function definitions match token-for-token after normalizing
upstream `double` to our `DLReal` and removing relocated function declarations.
The historical copy also matches under that comparison. Includes, namespace,
precision, disabled threading and prototype placement remain intentional local
adaptations; literal byte equality with upstream is not expected.

**Decision: retain the implementation.** No upstream C algorithm change was
missing. Preserve these adaptations if a future upstream release changes the
source; do not replace the file just to make its timestamp or hash match.
See the [comparison and validation record](../../../docs/review-2026-10/ooura-and-roadmap-2026-10-21.md)
for method, results and limits.
