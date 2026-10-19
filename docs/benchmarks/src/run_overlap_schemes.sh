#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Build and run the overlap-add vs overlap-save benchmark against single-precision FFTW.
# Usage: sh docs/benchmarks/src/run_overlap_schemes.sh [--quick] [> results.csv]
# Needs a C++20 compiler and FFTW3 (macOS: brew install fftw; Ubuntu: apt-get install libfftw3-dev).
# Set CXX or CXXFLAGS to override the compiler or flags.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/convolver_overlap_schemes
cxx=${CXX:-c++}

if pkg-config --exists fftw3f 2>/dev/null; then
    fftw_flags=$(pkg-config --cflags --libs fftw3f)
elif command -v brew >/dev/null 2>&1 && prefix=$(brew --prefix fftw 2>/dev/null); then
    fftw_flags="-I$prefix/include -L$prefix/lib -lfftw3f"
else
    fftw_flags="-lfftw3f"
fi

# shellcheck disable=SC2086 # Flags are intentionally split into words.
"$cxx" -std=c++20 ${CXXFLAGS:--O3 -DNDEBUG} "$here/overlap_schemes.cpp" $fftw_flags -o "$out"
echo "# $("$cxx" --version | head -n 1); $(uname -sm)" >&2
"$out" "$@"
