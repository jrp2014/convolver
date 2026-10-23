<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver history

[Current documentation](README.md) · [Current implementation](develop/architecture.md)

This page is historical context. New users can build and use the portable
implementation without reading it.

**In short:** Convolver was a set of Windows audio applications (2005–2006).
This repository rebuilds their history from SourceForge SVN and source zips,
plus recovered 4.4 material. That code is kept, unbuilt, in `historical/` and
checked by extraction tests (the folder was called `convolver/` until 21 October
2026, so older commits and records use that path). The portable C++20
implementation replaced it. Later
programs mostly adopted the `.cfg` format rather than the code; see
[downstream use](#downstream-use).

Convolver began as a set of Windows applications written in 2005 and 2006. Their
user documentation is still at <https://convolver.sourceforge.net/>, and an
archived copy of the 4.4 website is in [historical/convolver-4.4](historical/convolver-4.4/README.md).
This page records what the historical code is, where this repository's copy of
it came from, and how the portable implementation replaced it.

## The historical applications

The historical Windows applications by jrp are preserved separately: WMP DSP
(`convolverWMP`), DirectShow (`convolverFilter`), VST2 (`convolverVST`), command-line
(`convolverCMD`) and recovered 4.4 streaming [`convolverPipe`](convolverPipe.md).
They share the old overlap-save engine in
[`historical/convolution`](../historical/convolution); the portable engine uses
partitioned overlap-add. The [downstream evidence](review-2026-10/downstream-evidence.json)
records the derivative code that was found and the wider adoption of the text
format; see [downstream use](#downstream-use).

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
identifiers. See the [comparison and recovery findings](review-2026-10/reconciliation.md)
for provenance, exclusions and validation limits. The subsequent
[feature and documentation recovery](review-2026-10/archive-recovery.md)
restores `convolverPipe` and the [historical 4.4 website](historical/convolver-4.4/README.md),
including configuration examples, host guides and release history.

## Building the historical Windows applications

The original build used Visual Studio 2005 (some projects also have Intel C++
`.icproj` files). Open `historical/convolver.sln`. Beyond Visual Studio with ATL
and the Windows SDK, it expects:

- the DirectShow base classes (`streams.h`) for `convolverFilter`
- the Steinberg VST 2.x SDK and VSTGUI for `convolverVST`
- Boost (header-only parts)
- FFTW 3 and libsndfile DLLs (only headers and import wrappers are in this repository)

Native builds of these historical applications have not yet been verified.
The top-level CMake project builds the portable implementation; it does not
build these historical Windows applications.

## Historical regression checks

With CMake 3.20+, Python 3.8+ and Clang (including ASan/UBSan) on macOS or Linux:

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCONVOLVER_BUILD_NATIVE=OFF
ctest --test-dir build --output-on-failure --parallel 2
```

These checks cover extracted production routines and preservation of the
historical documentation. `python3 tests/regression/run_all.py` remains available.
The default native build separately tests the actual portable implementation,
FFT backends, libsndfile, CLI and external consumers. See [testing instructions and coverage limits](develop/testing.md).

## From historical to portable

The portable core and CLI, three FFT backends, offline FFTW planning, package
exports, configuration inspection, Windows-style relative paths, the
historical-only output features (dither, noise shaping, PCM8, valid bits,
automatic attenuation), the host lifecycle and VST3 are implemented. The
[implementation map](develop/architecture.md) describes the result;
[TODO](../TODO.md) holds the remaining interoperability, DAW/release validation
and measured optimization work, and the [modernization direction](modernization.md)
explains its priorities.
Earlier 4.3 assessments are in the [review archive](archive/reviews/README.md).

### How the portable version differs

Both engines use uniformly partitioned FFT convolution in single precision and
read the same `.cfg` syntax. The main differences, checked against the source
at 0.4:

| | Historical Windows applications (4.x) | Portable implementation |
| --- | --- | --- |
| Platforms and hosts | Windows only: WMP plug-in, DirectShow filter, VST 2, `convolverCMD`, `convolverPipe` | macOS, Linux and Windows: CLI, VST3 plug-in and C++ libraries |
| Choosing latency | You choose the number of partitions; latency is the partition length, so it grows with filter length | You choose the block size B (default 256); latency is B frames whatever the filter length |
| Algorithm | Overlap-save, accumulating a ring of output spectra | Overlap-add with a ring of input spectra, shared between paths with the same input mix |
| Filter lengths | Every path's IR must have the same length | Paths may use IRs of different lengths |
| Rendering a file | Output has the input's length: the processing delay is kept and the filter tail cut off | Processing delay removed and the whole tail written (`--keep-latency` keeps the delay) |
| Changing filters in a plug-in | Rebuilt while holding the audio lock, so FFTW planning can interrupt audio | Prepared off the audio thread, then crossfaded in; the audio path does not allocate, lock or read files |
| Saved plug-in state | The filter's file path | The filter itself, so a project still loads if the files move |
| FFTW planning | Chosen per instance, with a `wisdom.fftw` file beside the program | Fast estimate by default; plans tuned once with `--tune-fft` and loaded with `--fft-wisdom` |
| Configuration paths | Windows paths; the bundled samples use absolute drive paths | Relative to the configuration on every platform; `\` separators accepted; drive letters explained, not guessed |

Features once found only in the historical engine were ported in 0.4.0: dither
and the five noise shapers on every integer format, PCM8 output, valid-bits
output, the optimum-attenuation estimate, list selection by output channels,
exhaustive and time-limited FFTW planning, the Ooura FFT and plug-in buses of up
to 8 channels. What remains only in the historical code is the Windows media
player integration (WMP and DirectShow), the VST 2 build and double-precision
output formats. The bundled 2005–2006 sample configurations predate the 3.0
format or use absolute paths, so they need editing before the portable reader
accepts them; see [bundled samples](configuration-compatibility.md#bundled-historical-samples).

## Downstream use

The [17 October investigation](review-2026-10/downstream-evidence.json) records
URLs, hashes, binary markers and attribution. It distinguishes:

- **Album Player:** evidence of a Windows ConvolverVST adaptation and Linux core
  lineage. Binaries were inspected, not executed; no source diff or bug-status
  conclusion was established.
- **DRC Designer/cPlay:** distribution/integration or external plug-in hosting,
  not evidence of another embedded engine fork.
- **Audiolense/JRiver, Roon, Hang Loose Convolver and CamillaGUI/CamillaDSP:**
  configuration-format adoption or import. This does not establish DSP source reuse.
- **Equalizer APO:** the investigation identifies a different convolution-library
  lineage.

These are dated findings, not a survey of every current release. The
[inspection script](review-2026-10/inspect_downstream.py) can recheck artifacts,
but upstream downloads change and are not a normal offline CI dependency.
Compare actual source and tests before importing any downstream fix.
[Configuration compatibility](configuration-compatibility.md) owns the current
format-adoption matrix and its evidence limits.

## Documentation snapshots

The [pre-refresh documentation](archive/documentation-2026-10-20/README.md)
retains the complete text of the guides before the 20 October reorganization.
Use the [review record](archive/reviews/2026-10-20-documentation-review.md) to find their current
successors. Dated validation and benchmark results retain their original scope.
