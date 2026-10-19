# Ooura FFT

`fftsg.cpp` is Takuya Ooura's general-purpose FFT package (`fftsg.c`, split-radix,
1996–2001), used by the optional `OOURA` FFT backend (`portable/fft_ooura.cpp`)
for builds without an FFT library dependency.

Licence (from the package): "You may use, copy, modify and distribute this code
for any purpose (include commercial use) and without fee. Please refer to this
package when you modify this code." This permissive licence is compatible with
Convolver's GPL-2.0-or-later. Upstream: <http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html>.

The copy is taken from the historical engine's `convolver/fft/fftsg.cpp` (the
version distributed with DRC, which names the sample type `DLReal`). Changes,
listed at the top of the file: single precision, a private C++ namespace, thread
support removed, `<cmath>` included outside the namespace. The transform code is
otherwise unchanged; it is third-party code and is not reformatted or linted.
`tests/native/fft_tests.cpp` checks the backend against a direct DFT.
