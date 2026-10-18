# Convolver

Convolver applies impulse-response filters for room correction, crossovers and
multichannel audio routing. The current **portable 0.2.0** implementation provides
a C++20 core, file/RAW-stream CLI and optional VST3 plug-in, using CMake, FFTW or
Apple Accelerate, and libsndfile at the file boundary.

Start with the [portable guide](docs/portable-core.md),
[package guide](docs/cmake-package.md) or [VST3 guide](docs/vst3.md).
The [current repository review](REVIEW.md) records correctness, completed
milestones, verification evidence and remaining compatibility work.

The historical Windows applications by jrp are preserved separately: WMP DSP
(`convolverWMP`), DirectShow (`convolverFilter`), VST2 (`convolverVST`), command-line
(`convolverCMD`) and recovered 4.4 streaming [`convolverPipe`](docs/convolverPipe.md).
They share the old overlap-save engine in
[`convolver/convolution`](convolver/convolution); the portable engine uses
partitioned overlap-add. The [downstream evidence](docs/review-2026-10/downstream-evidence.json)
documents both derivative implementations and wider adoption of the text format.

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

On Apple platforms, add `-DCONVOLVER_FFT_BACKEND=ACCELERATE` to use the system
FFT framework without FFTW. See [backend measurements](docs/fft-backends.md).

See [platform instructions and options](docs/portable-core.md) for macOS/Linux
packages, Windows/vcpkg, sanitizer builds and CLI semantics.

For a core-only build, installed CMake targets and repeatable presets, see
[the package guide](docs/cmake-package.md). Contributors can use the
[formatting, lint and testing workflow](CONTRIBUTING.md).

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
The default native build additionally runs eight suites against the actual core,
the selected FFT backend, libsndfile and CLI, plus an external package-consumer suite. See [testing instructions and coverage limits](docs/testing.md).

## Modernization

Core/CLI, FFTW/Accelerate, offline planning, split-spectrum MACs, package exports,
Windows config paths, host lifecycle and VST3 are implemented. The
[structure review](docs/project-structure.md) and
[modernization plan](docs/modernization.md) identify remaining dither,
interoperability, DAW/release validation and measured optimization work.
Earlier 4.3 assessments are in the [review archive](docs/archive/reviews/README.md).

Rules for contributors and coding agents are in [AGENTS.md](AGENTS.md).

## Licence

GNU General Public License, version 2 or (at your option) any later version.
See [COPYING](COPYING).
