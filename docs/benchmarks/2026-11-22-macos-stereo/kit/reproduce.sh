#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Run on Apple silicon with clang++, CMake, Python 3 and libsndfile development files.
set -eu
if [ "$#" -ne 2 ]; then
  echo "Usage: sh $0 /path/to/convolver /new/benchmark-directory" >&2
  exit 2
fi
src=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
expected=9cbcb6b1c0c7a9cca07c29a852e5172ff3cd6376
git -C "$src" cat-file -e "$expected^{commit}"
git -C "$src" cat-file -e 'ee3323f2078846fef8c30da19771820f3f2f793d^{commit}'
git -C "$src" cat-file -e '8c0814eb826fcc421fc61c9090891043f80169e5^{commit}'
mkdir "$2"
cd "$2"
work=$(pwd)
cp -R "$here" kit
git clone --quiet --local --no-checkout "$src" current
git -C current checkout --quiet --detach "$expected"
curl -fsSL https://fftw.org/fftw-3.3.11.tar.gz -o fftw.tar.gz
curl -fsSL https://archives.boost.io/release/1.33.1/source/boost_1_33_1.tar.bz2 -o boost.tar.bz2
echo "5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1  fftw.tar.gz" | shasum -a 256 -c -
echo "6232e93205acbc8c705f44f15977aae158550c99a384f41606cff26c16393be0  boost.tar.bz2" | shasum -a 256 -c -
tar -xzf fftw.tar.gz
tar -xjf boost.tar.bz2 boost_1_33_1/boost
(cd fftw-3.3.11 && ./configure --prefix="$work/deps" --enable-float --enable-neon --enable-threads --disable-fortran --disable-shared --enable-static && make -j8 && make install) > fftw-build.log 2>&1
cmake -S current -B fftw -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_FFT_BACKEND=FFTW -DCMAKE_DISABLE_FIND_PACKAGE_FFTW3f=ON -DCMAKE_PREFIX_PATH="$work/deps" -DBUILD_TESTING=OFF
cmake -S current -B accelerate -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_FFT_BACKEND=ACCELERATE -DBUILD_TESTING=OFF
cmake --build fftw --target convolver_core --parallel 6
cmake --build accelerate --target convolver_core --parallel 6
python3 kit/generate_legacy.py "$work/current" legacy.inc
python3 kit/fixtures.py > fixtures.txt
python3 kit/provenance.py "$work/current" > provenance.txt
sh kit/build-binaries.sh
python3 kit/run.py warm
python3 kit/run.py legacy-style
python3 kit/run.py warm-long
python3 kit/archive_sweep.py
python3 kit/check.py
