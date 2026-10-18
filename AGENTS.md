# Guidance for coding agents and contributors

Convolver applies multichannel FIR filters to audio (GPL-2.0-or-later). The repo
holds two implementations:

- **Portable core (active):** C++20 library, CLI and VST3 adapter built with
  CMake. `include/convolver/` (public API), `portable/` (engine, host lifecycle,
  FFT backends, libsndfile I/O, `.cfg` loader, CLI), `plugins/vst3/`,
  `tests/native/`, `tests/consumer/`.
- **Historical Windows code (preserved):** `convolver/` — the 2005–2006 engine
  and DirectShow/WMP/VST 2/CMD/Pipe front ends. It needs a Visual Studio
  2005-era toolchain and is not built by CI. Fix only proven defects there.

Start with `CONTRIBUTING.md` (build, presets, formatting, analysis) and
`REVIEW.md` (current review and priorities). Plans: `docs/project-structure.md`;
behaviour: `docs/portable-core.md`, `docs/testing.md`, `docs/vst3.md`.

## Build and test

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCONVOLVER_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --parallel 4
```

Needs CMake 3.20+, Python 3.8+, FFTW3 (single precision) and libsndfile. On Ubuntu:
`apt-get install cmake libfftw3-dev libsndfile1-dev libclang-rt-18-dev clang-format-18 clang-tidy-18`.
Claude Code cloud sessions install these via `.claude/hooks/session-start.sh`.
CI runs Linux (Clang + GCC), macOS (FFTW and Accelerate) and Windows (MSVC, vcpkg).

- Every DSP change must pass the direct double-precision oracle tests in
  `tests/native/engine_tests.cpp`; report the worst error you observed.
- `tests/regression/` compiles **historical** functions by slicing
  `convolver/` source on text markers. If you edit those files, update the
  matching `tests/regression/*.py` in the same change.
- Portable sources must pass `clang-format-18 --dry-run --Werror` and the
  `.clang-tidy` checks (CI enforces both; commands in `CONTRIBUTING.md`).
- Do not claim something builds or passes unless you ran it.

## Rules

- Real-time path: `Engine::process` and everything it calls must not allocate,
  lock or do I/O (`engine_tests.cpp` checks allocation). Preparation, FFTW
  planning and wisdom stay outside processing.
- `.cfg` compatibility is a product feature (Roon, JRiver and Audiolense use it).
  Preserve relative paths, signed-zero routes, fractional route gains, delay
  truncation and list selection. Syntax changes need a recorded decision.
- Keep component boundaries in `docs/project-structure.md`; split files only
  when a responsibility grows.
- `docs/historical/` files are byte-exact copies with recorded hashes; never edit
  them. Do not reformat files under `convolver/`; it destroys `git blame`.
- Line endings: LF in the repo; `.sln`, `.vcproj`, `.rc` are CRLF via `.gitattributes`.
- Vendored third-party code (`convolver/Common/`, `convolver/fftw/`,
  `convolver/libsndfile/` headers) is not ours to patch.
- New files carry `// SPDX-License-Identifier: GPL-2.0-or-later`.
- Keep commits focused; state the audible or numerical effect of a DSP change.
