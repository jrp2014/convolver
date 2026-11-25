<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# FFT-5

## Fixed

- Estimate (FFT-5): whether a filter near the largest float gets a noise
  estimate no longer depends on the build. One tap of 3e38 got no estimate
  in one Linux build and an estimate in others, because the noise could
  overflow inside the FFT; now every filter with a No clipping figure has
  an estimate too, in the plug-ins and with `--gain-db auto`, which refuses
  only a filter that `no-clip` refuses as well. Ordinary filters' figures
  are unchanged, bit for bit. The plug-ins' `Too loud to estimate: No
  clipping's gain plays.` can no longer happen and is gone. See
  [gain and attenuation](../docs/use/command-line.md#gain-and-attenuation).

## For contributors

- Engine (FFT-5): `estimate_attenuation()` filters the noise through a copy
  of the filter whose input gains, taps and output gains are scaled by
  powers of two to fixed levels well inside the float range, then scales
  the peak back in double,
  caps it at the bound and reads output below the smallest normal float as
  silence; it throws `std::overflow_error` only when that peak is beyond
  the largest float. `native.engine` `attenuation` checks that the estimate
  scales exactly from 2^-100 to 2^125, by taps or by route gains, and that
  a quiet route beside cancelling 1e38 routes is kept (from Codex's review);
  `native.host_settings` `filter_estimates` and `status_names_settings`
  check that 3e38 and largest-float taps have both figures. FFTW's plan
  changed output only by rounding at every level tested, and one tap above
  2^127 overflowed in the FFT stages with every plan
  ([extreme levels and the FFTW plan](../docs/develop/fft-measurements.md#extreme-levels-and-the-fftw-plan)).

## Done

- [x] **FFT-5 — Estimate's overflow decided the same in every build.**
  25 November 2026, PR #286
  ([extreme levels and the FFTW plan](../docs/develop/fft-measurements.md#extreme-levels-and-the-fftw-plan)).
