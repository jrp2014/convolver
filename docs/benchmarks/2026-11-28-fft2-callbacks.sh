#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# FFT-2: per-instance CPU through HostProcessor with each FFT backend.
# Alternates three Release builds (CONVOLVER_BUILD_BENCHMARKS=ON) over the
# same workloads and prints one CSV row per run: round, workload, build,
# then convolver_callback_benchmark's own row.
# Usage: 2026-11-28-fft2-callbacks.sh ROUNDS NAME=BUILD_DIR...
set -euo pipefail
rounds=$1
shift
workloads=(
  "corr65k-64|--taps 65536 --inputs 2 --outputs 2 --layout diagonal --callback 64"
  "corr65k-128|--taps 65536 --inputs 2 --outputs 2 --layout diagonal --callback 128"
  "corr16k-64|--taps 16384 --inputs 2 --outputs 2 --layout diagonal --callback 64"
  "xover65k-64|--taps 65536 --inputs 2 --outputs 6 --layout crossover --callback 64"
  "ts3s-128|--taps 144000 --inputs 2 --outputs 2 --layout matrix --callback 128 --filter decaying"
  "rev30s-64|--taps 1440000 --inputs 2 --outputs 2 --layout diagonal --callback 64 --filter decaying"
  "rev30s-128|--taps 1440000 --inputs 2 --outputs 2 --layout diagonal --callback 128 --filter decaying"
)
for round in $(seq 1 "$rounds"); do
  for workload in "${workloads[@]}"; do
    name=${workload%%|*}
    read -r -a options <<<"${workload#*|}"
    for build in "$@"; do
      line=$("${build#*=}/convolver_callback_benchmark" "${options[@]}" --block 0 --seconds 2 | tail -1)
      echo "$round,$name,${build%%=*},$line"
    done
  done
done
