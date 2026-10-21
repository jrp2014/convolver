#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Cloud sessions: install what the CMake build and tests need (CMake, FFTW,
# libsndfile, Clang sanitizer runtimes). Local machines are left alone.
set -euo pipefail
[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0
[ "$(uname -s)" = "Linux" ] || exit 0
command -v apt-get >/dev/null || exit 0
if command -v cmake >/dev/null && command -v clang-format-18 >/dev/null && command -v clang-tidy-18 >/dev/null && [ -f /usr/include/fftw3.h ] && [ -f /usr/include/sndfile.h ] &&
   ls /usr/lib/llvm-*/lib/clang/*/lib/linux/libclang_rt.asan-x86_64.a >/dev/null 2>&1; then
  exit 0
fi
ver=$(clang++ --version 2>/dev/null | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)
ver=${ver:-18}
SUDO=""; [ "$(id -u)" = 0 ] || SUDO="sudo"
pkgs=(cmake libfftw3-dev libsndfile1-dev "libclang-rt-${ver}-dev" clang-format-18 clang-tidy-18)
$SUDO apt-get install -y "${pkgs[@]}" >/dev/null 2>&1 || {
  $SUDO apt-get update -qq >/dev/null 2>&1
  $SUDO apt-get install -y "${pkgs[@]}" >/dev/null 2>&1
} || echo "Could not install: ${pkgs[*]}; the CMake build or sanitizer tests may fail." >&2
