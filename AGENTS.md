<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Guidance for coding agents and contributors

Convolver applies multichannel FIR filters to audio (GPL-2.0-or-later). The repo
holds two implementations:

- **Portable core (active):** C++20 library, CLI and plug-in adapters built
  with CMake. `include/convolver/` (public API), `portable/` (engine, host
  lifecycle, FFT backends, libsndfile I/O, `.cfg` loader, CLI), `plugins/`
  (one folder per format: `plugins/vst3/`, `plugins/lv2/` and `plugins/au/`),
  `tests/` (one folder per area), `scripts/release/` and `packaging/`
  (preview release builds).
- **Historical Windows code (preserved):** `historical/` — the 2005–2006 engine
  and DirectShow/WMP/VST 2/CMD/Pipe front ends. It needs a Visual Studio
  2005-era toolchain and is not built by CI. Fix only proven defects there.

Start with `CONTRIBUTING.md` (formatting, analysis, lint), `REVIEW.md`
(current status) and `TODO.md` (open work and priorities). The pages written
for contributors and agents are in `docs/develop/`: build commands in
`docs/develop/build.md`, the implementation map in
`docs/develop/architecture.md`, tests in `docs/develop/testing.md`, the
plug-in rules in `docs/develop/plugin-contract.md` and the procedures in
`docs/develop/workflow.md`. User behaviour is described in `docs/use/`: the
command line in `docs/use/command-line.md`, configurations in
`docs/use/configuration.md` and the plug-ins in `docs/use/plugins.md`. Open plans are in `docs/plans/`;
the plug-in direction is `docs/plans/2026-10-21-plugin-formats.md`. How the
documentation is organised and written, for agents too, is in
`docs/develop/documentation.md`.

## Taking on work

Follow `docs/develop/workflow.md`. In short:

- One TODO item (or a tightly coupled group) per branch. Open a **draft pull
  request titled with the ID** as soon as you start; that is the claim. Check
  open pull requests for the ID first.
- Keep the **handoff note** in the pull request description current, and push
  at every stopping point: cloud sessions are discarded without warning.
- Ask jrp before deciding anything permanent or user-facing (identities,
  state/preset formats, `.cfg` syntax, licences, sound-changing defaults,
  shipped dependencies, publishing).
- Before marking ready: the definition of done in the workflow guide, and an
  **independent review** by someone who did not write the change.
- jrp merges. Never merge, approve, push to `main` or move tags.

Claude Code sessions have two repository skills, `todo-item` (start, resume or
hand over an item) and `review-change` (the independent review). Both only
point to sections of `docs/develop/workflow.md`, which holds the procedures.

## Build and test

Use the sanitizer build for every change (all builds, presets and plug-in SDKs
are in `docs/develop/build.md`):

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCONVOLVER_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --parallel 4
```

The default build needs CMake 3.20+, Python 3.8+, FFTW3 (single precision) and
libsndfile. Accelerate (Apple only) or built-in Ooura can replace FFTW; see
[backend selection](docs/develop/fft-backends.md). On Ubuntu:
`apt-get install cmake libfftw3-dev libsndfile1-dev libclang-rt-18-dev clang-format-18 clang-tidy-18`.
Claude Code cloud sessions install these via `.claude/hooks/session-start.sh`.
CI runs Linux (Clang + GCC), macOS (FFTW and Accelerate) and Windows (MSVC, vcpkg),
plus Ooura configurations described in [testing](docs/develop/testing.md).

- Every DSP change must pass the direct double-precision oracle tests in
  `tests/native/engine_tests.cpp`; report the worst error you observed.
- `tests/regression/` compiles **historical** functions by slicing
  `historical/` source on text markers. If you edit those files, update the
  matching `tests/regression/*.py` in the same change.
- Run `pre-commit run --all-files` before **every** push, draft commits
  included. It formats the portable C++ with clang-format 18 (also
  `python3 scripts/check_format.py --fix`) and runs the other lint checks;
  lint CI fails on any difference. Cloud sessions install it as a Git hook.
  Portable sources must also pass the `.clang-tidy` checks (commands in
  `CONTRIBUTING.md`).
- `python3 scripts/check_repository.py` must pass: TODO IDs defined, every
  maintained page indexed, plans dated and current, paths in this file real,
  SPDX lines, and the top-level layout. Lint CI runs it through pre-commit.
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
  follow [the versioning guide](docs/develop/versioning.md): the helper creates commits
  and tags even without `--push`, so it is **not a dry run**.

## Rules

- Documentation: follow [page ownership and maintenance](docs/develop/documentation.md).
  Use [the implementation map](docs/develop/architecture.md) to find symbols,
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
- Keep component boundaries in `docs/develop/architecture.md`; split files only
  when a responsibility grows.
- `docs/historical/` files are byte-exact copies with recorded hashes; never edit
  them. Do not reformat files under `historical/`; it destroys `git blame`.
- Line endings: LF in the repo; `.sln`, `.vcproj`, `.rc` are CRLF via `.gitattributes`.
- Vendored third-party code (`historical/Common/`, `historical/fftw/`,
  `historical/libsndfile/` headers) is not ours to patch.
- New files carry `// SPDX-License-Identifier: GPL-2.0-or-later`.
- Keep commits focused; state the audible or numerical effect of a DSP change.
