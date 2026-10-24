<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Apple Silicon vDSP accumulation experiment

PERF-1 experiment requested by jrp on 24 October 2026. Base revision:
`884c27e4db9f1630841fc96b9cd1e6c1ed5daadc`.

Compare the current compiler-vectorised split-complex kernels with
Accelerate `vDSP_zvmul` for the first partition and `vDSP_zvma` for subsequent
partitions. Keep the Accelerate FFT, spectrum layout, scaling, partition
order and compiler flags identical. No FFT-boundary conversion changes.
The candidate is opt-in and is not a decision to change shipped defaults.

Run the existing sanitizer tests and double-precision oracle checks before
measuring Release builds. Measure both the production partition kernel and
the existing whole-engine workloads in alternating order, retaining every
sample. Report medians and variability, numerical divergence, machine and
compiler details, and any failed checks. No performance result recorded yet.
