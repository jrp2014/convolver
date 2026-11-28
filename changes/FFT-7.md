<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# FFT-7

## For contributors

- FFT (FFT-7): PFFFT is a supported Mac build option beside Accelerate,
  which stays the default and what the Mac packages ship:
  `-DCONVOLVER_FFT_BACKEND=PFFFT` builds it into the CLI and every plug-in
  (NEON on Apple silicon, SSE on Intel). The Audio Unit then carries PFFFT's
  and Ooura's notices inside the component, as it does its own (an Ooura
  build carries Ooura's), and `au.bundle` checks them. The Audio Unit
  workflow builds and tests the component with PFFFT too. See
  [PFFFT](../docs/develop/fft-backends.md#pffft-on-linux-and-windows).

## Done

- [x] **FFT-7 — PFFFT as a Mac build option.** 28 November 2026, at jrp's
  request: Mac plug-ins can be built with PFFFT beside Accelerate, with the
  Audio Unit's notices and CI coverage; Accelerate stays the default
  ([PFFFT](../docs/develop/fft-backends.md#pffft-on-linux-and-windows)).
