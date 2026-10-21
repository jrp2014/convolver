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

Start with `CONTRIBUTING.md` (build, presets, formatting, analysis),
`REVIEW.md` (current status) and `TODO.md` (open work and priorities).
Implementation: `docs/project-structure.md`; behaviour: `docs/portable-core.md`,
`docs/configuration.md`, `docs/testing.md`, `docs/vst3.md`.

## Build and test

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCONVOLVER_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --parallel 4
```

The default build needs CMake 3.20+, Python 3.8+, FFTW3 (single precision) and
libsndfile. Accelerate (Apple only) or built-in Ooura can replace FFTW; see
[backend selection](docs/fft-backends.md). On Ubuntu:
`apt-get install cmake libfftw3-dev libsndfile1-dev libclang-rt-18-dev clang-format-18 clang-tidy-18`.
Claude Code cloud sessions install these via `.claude/hooks/session-start.sh`.
CI runs Linux (Clang + GCC), macOS (FFTW and Accelerate) and Windows (MSVC, vcpkg),
plus Ooura configurations described in [testing](docs/testing.md).

- Every DSP change must pass the direct double-precision oracle tests in
  `tests/native/engine_tests.cpp`; report the worst error you observed.
- `tests/regression/` compiles **historical** functions by slicing
  `convolver/` source on text markers. If you edit those files, update the
  matching `tests/regression/*.py` in the same change.
- Portable sources must pass `clang-format-18 --dry-run --Werror` and the
  `.clang-tidy` checks (CI enforces both; commands in `CONTRIBUTING.md`).
- Do not claim something builds or passes unless you ran it.

## Versioning

- `VERSION` is the single `MAJOR.MINOR.PATCH` source for the portable CMake
  package, CLI and VST3 bundle. Do not hardcode copies elsewhere. Historical
  Windows resource versions and `convolver*` tags have separate histories.
- For ordinary changes, leave `VERSION` and tags alone. After a push to `main`,
  **Version source changes** increments PATCH, commits `VERSION` and atomically
  publishes an annotated `vX.Y.Z` tag. Feature branches and PRs do not create tags.
  Concurrent pushes may be grouped into one version; repeat runs are no-ops.
- `SOURCE_PATHS` in `scripts/version_source.py` defines what counts: sources,
  tests, CMake files, scripts and workflows. Changes confined to `docs/` or
  top-level Markdown files (including this file) do not bump the version.
- For an intentional minor/major milestone, edit `VERSION` in that change and
  reset the lower components (for example, `0.3.0`). The workflow honours the
  higher number. Before 1.0, API incompatibilities belong in minor versions;
  preserve the package's matching-minor compatibility policy. Record noteworthy
  changes in `CHANGELOG.md`; do not change plug-in IDs or state-format versions
  merely to match a package version.
- On `main`, use `git pull --ff-only` after the bot's version commit; fetch tags
  when looking up versions. Never move or overwrite published tags. CLI/VST3
  build identifiers append the Git revision and `.dirty` for uncommitted files;
  numeric package/bundle versions remain `MAJOR.MINOR.PATCH`.
- Source tags identify revisions, not passing CI or production binary releases.
  Test versioning with `python3 tests/versioning_tests.py`. For workflow recovery,
  follow [the versioning guide](docs/versioning.md): the helper creates commits
  and tags even without `--push`, so it is **not a dry run**.

## Rules

- Documentation: follow [page ownership and maintenance](docs/documentation.md).
  Use [the implementation map](docs/project-structure.md) to find symbols,
  invariants and tests. Update the owning guide when behavior changes; keep open
  work and documented limitations in [TODO.md](TODO.md). Run the documentation
  checks in `CONTRIBUTING.md` and distinguish actual test results from configured
  CI coverage. Preserve dated evidence and historical originals.

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
