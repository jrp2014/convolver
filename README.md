# Convolver

Convolver is a Windows audio convolution engine by John Pavel. It applies
impulse-response filters (typically for digital room correction or crossovers)
to multichannel audio in real time, and ships as:

- a Windows Media Player DSP plug-in (`convolverWMP`)
- a DirectShow filter (`convolverFilter`)
- a VST plug-in (`convolverVST`)
- a command-line tool (`convolverCMD`)
- a stdin/stdout streaming tool ([`convolverPipe`](docs/convolverPipe.md)), recovered from the historical 4.4 sources

The historical applications share the engine in [`convolver/convolution`](convolver/convolution),
which does uniformly partitioned overlap-save convolution with FFTW.

A new **portable C++20 core and CLI** in [`portable/`](portable) builds with
CMake, FFTW and libsndfile. It supports native file rendering, RAW streaming and
the historical configuration format. See the [portable build and usage guide](docs/portable-core.md).

Its text config format (`.cfg`) is still read by Roon, JRiver Media Center and
Audiolense today.

## Where this code came from

The project lived on SourceForge from 2005 to 2006:

- Project page and file releases: <https://sourceforge.net/projects/convolver/files/>
- Documentation: <https://convolver.sourceforge.net/index.html>

The history in this repository was rebuilt from two SourceForge sources:

1. **SVN history, May 2005 to March 2006** (`convolver1-7` to `convolver3-5`).
   SourceForge's SVN repository (originally CVS, converted by cvs2svn) was
   replayed commit by commit from `/trunk`. Each commit keeps its original
   date, message and a link to the SVN revision it came from. The CVSROOT
   administrative files were left out, and SVN tags became git tags.
2. **Source releases 3.7, 4.0 and 4.3, April to July 2006.** Development moved
   off SourceForge CVS after 3.5, so these exist only as source zips. Each is
   one commit, tagged `convolver3-7`, `convolver4-0` and `convolver4-3`. CVS
   and SourceSafe metadata, build output, IDE caches and VTune projects were
   left out.

The imported SVN history was checked against the `convolver3-3src.zip`
release, and the `convolver4-3` tree against `convolver4-3src.zip`. Both match
apart from the excluded files and line endings.

The original import stopped at the 4.3 source release. A subsequently supplied
`homes.zip` contains corroborated 4.4 source snapshots and a separate tree
labelled 4.5, alongside a 4.5 prerelease installer. Selected improvements have
been recovered without replacing the reviewed engine or changing release
identifiers. See the [comparison and recovery findings](docs/review-2026-10/reconciliation.md)
for provenance, exclusions and validation limits. The subsequent
[feature and documentation recovery](docs/review-2026-10/archive-recovery.md)
restores `convolverPipe` and the [historical 4.4 website](docs/historical/convolver-4.4/README.md),
including configuration examples, host guides and release history.

## Building

For the portable implementation, install CMake, a C++20 compiler, FFTW (including
its float library) and libsndfile development packages:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
./build-native/convolver --filter impulse.wav --input music.wav --output corrected.wav
```

See [platform instructions and options](docs/portable-core.md) for macOS/Linux
packages, Windows/vcpkg, sanitizer builds and CLI semantics.

### Historical Windows builds

The original build used Visual Studio 2005 (some projects also have Intel C++
`.icproj` files). Open `convolver/convolver.sln`. Beyond Visual Studio with ATL
and the Windows SDK, it expects:

- the DirectShow base classes (`streams.h`) for `convolverFilter`
- the Steinberg VST 2.x SDK and VSTGUI for `convolverVST`
- Boost (header-only parts)
- FFTW 3 and libsndfile DLLs (only headers and import wrappers are in this repository)

Native builds of these historical applications have not yet been verified.
The top-level CMake project builds the portable implementation; it does not
build these historical Windows applications.

## Portable regression checks

With CMake 3.20+, Python 3.8+ and Clang (including ASan/UBSan) on macOS or Linux:

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCONVOLVER_BUILD_NATIVE=OFF
ctest --test-dir build --output-on-failure --parallel 2
```

These nine suites cover extracted production routines and preservation of the
historical documentation. `python3 tests/regression/run_all.py` remains available.
The default native build additionally runs three suites against the actual core,
FFTW, libsndfile and CLI. See [testing instructions and coverage limits](docs/testing.md).

## Modernization

The [modernization assessment](docs/modernization.md) recommends a portable C++
core and CLI, CMake builds, FFTW/vDSP backends with PFFFT benchmarking, retained
libsndfile support, and staged performance work. The initial portable core/CLI
milestone is implemented; alternative backends, plug-ins and performance tuning
remain future work.

## Licence

GNU General Public License, version 2 or (at your option) any later version.
See [COPYING](COPYING).
