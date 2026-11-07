#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# PERF-2 experiment: build the long-reverb benchmark with KlangFalter's engine
# (HiFi-LoFi/FFTConvolver, MIT, fetched at a pinned commit into the build
# directory, never into the source tree) and print one CSV row per workload.
# Usage: sh tests/native/benchmarks/run_long_reverb.sh [--quick] > results.csv
# Needs CMake, a C++20 compiler, git and the default build's libraries.
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
build=${BUILD_DIR:-$root/build-long-reverb}
fftconvolver_commit=f2cdeb04c42141d2caec19ca4f137398b2a76b85
seconds=5
case "${1:-}" in --quick) seconds=1 ;; esac

mkdir -p "$build"
if [ ! -d "$build/FFTConvolver/.git" ]; then
    git clone --quiet https://github.com/HiFi-LoFi/FFTConvolver.git "$build/FFTConvolver" >&2
fi
git -C "$build/FFTConvolver" checkout --quiet "$fftconvolver_commit"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_BUILD_BENCHMARKS=ON \
    -DBUILD_TESTING=OFF -DCONVOLVER_FFTCONVOLVER_DIR="$build/FFTConvolver" >&2
cmake --build "$build" --target convolver_long_reverb_benchmark --parallel >&2
bench="$build/convolver_long_reverb_benchmark"
echo "# $(uname -sm); $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 || sysctl -n machdep.cpu.brand_string 2>/dev/null)" >&2

"$bench" --mode header
run() { "$bench" --seconds "$seconds" "$@"; }
# The candidate layout: first block 64, largest 8192, growth 4.
nu="--candidate nonuniform --first 64 --largest 8192 --ratio 4"
for ir in 10 30 60; do
    for cb in 64 256 1024; do
        run --candidate engine --block 256 --ir-seconds "$ir" --callback "$cb"
        run --candidate engine --block 1024 --ir-seconds "$ir" --callback "$cb"
        run $nu --schedule immediate --ir-seconds "$ir" --callback "$cb"
        run $nu --schedule spread --ir-seconds "$ir" --callback "$cb"
        run $nu --schedule spread --latency 256 --ir-seconds "$ir" --callback "$cb"
        run $nu --schedule worker --paced 1 --ir-seconds "$ir" --callback "$cb"
        run --candidate kf --ir-seconds "$ir" --callback "$cb"
        run --candidate kf-thread --paced 1 --ir-seconds "$ir" --callback "$cb"
    done
done
# Zero latency with today's uniform partitions is only affordable for short filters.
run --candidate engine --block 64 --ir-seconds 10 --callback 64
# True stereo (four paths) and 96 kHz.
for c in "--candidate engine --block 256" "$nu --schedule spread" "--candidate kf"; do
    # shellcheck disable=SC2086 # Options are intentionally split into words.
    run $c --paths 4 --ir-seconds 30 --callback 256
    # shellcheck disable=SC2086
    run $c --rate 96000 --ir-seconds 30 --callback 256
done
# Layout tuning at zero latency: largest block and growth.
for cb in 64 256; do
    for largest in 4096 8192 16384 32768; do
        for ratio in 2 4; do
            run --candidate nonuniform --schedule spread --first 64 --largest "$largest" \
                --ratio "$ratio" --ir-seconds 30 --callback "$cb"
        done
    done
done
