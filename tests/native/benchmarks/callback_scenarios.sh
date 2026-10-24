#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Compare two builds of convolver_callback_benchmark over the costly moments
# (startup, reset, live replacement), warmed processing and configuration
# loading, alternating the builds in each round.
# Usage: sh tests/native/benchmarks/callback_scenarios.sh BEFORE AFTER [ROUNDS] > results.csv
# BEFORE and AFTER are convolver_callback_benchmark executables from Release
# builds with -DCONVOLVER_BUILD_BENCHMARKS=ON. Each run takes 2 s; three rounds
# take about 15 minutes. Workload: 65,536-tap paths, a 6x6 matrix (36 paths) at
# B = 256, 257 and 1024 with 64-frame callbacks, and B = 256 with 512-frame
# callbacks; loading is a 2-to-6 crossover naming one six-channel WAV file.
set -eu
before=$1
after=$2
rounds=${3:-3}
round=1
while [ "$round" -le "$rounds" ]; do
    for cfg in "256 64" "257 64" "1024 64" "256 512"; do
        # shellcheck disable=SC2086 # Split "block callback" on purpose.
        set -- $cfg
        for scenario in warm startup reset replace; do
            for build in before after; do
                if [ "$build" = before ]; then exe=$before; else exe=$after; fi
                line=$("$exe" --scenario "$scenario" --block "$1" --callback "$2" --seconds 2 | tail -n 1)
                echo "$round,$build,$line"
            done
        done
    done
    for build in before after; do
        if [ "$build" = before ]; then exe=$before; else exe=$after; fi
        line=$("$exe" --scenario load --inputs 2 --outputs 6 --seconds 2 | tail -n 1)
        echo "$round,$build,$line"
    done
    round=$((round + 1))
done
