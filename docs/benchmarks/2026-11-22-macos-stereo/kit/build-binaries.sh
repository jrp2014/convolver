#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
c++ -std=c++14 -O3 -DNDEBUG -Iboost_1_33_1 -c kit/noise.cpp -o noise.o
for backend in fftw accelerate; do
  c++ -std=c++20 -fms-extensions -O3 -DNDEBUG -Wno-unknown-pragmas -Wno-ignored-attributes -I. -Icurrent/include -Ideps/include kit/bench.cpp noise.o "$backend"/libconvolver_core.a deps/lib/libfftw3f_threads.a deps/lib/libfftw3f.a -framework Accelerate -o "bench-$backend"
done
