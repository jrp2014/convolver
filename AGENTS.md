<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Guidance for coding agents and contributors

Convolver applies multichannel FIR filters to audio (GPL-2.0-or-later). The repo
holds two implementations:

- **Portable core (active):** C++20 library, CLI and plug-in adapters built
  with CMake. `include/convolver/` (public API), `portable/` (engine, host
  lifecycle, FFT backends, libsndfile I/O, `.cfg` loader, CLI), `plugins/`
  (one folder per format: `plugins/vst3/`, `plugins/lv2/` and `plugins/au/`,
  and `plugins/editor/` for the macOS window VST3 and the AU share),
  `tests/` (one folder per area), `scripts/release/` and `packaging/`
  (preview release builds).
- **Historical Windows code (preserved):** `historical/` — the 2005–2006 engine
  and DirectShow/WMP/VST 2/CMD/Pipe front ends. It needs a Visual Studio
  2005-era toolchain and is not built by CI. Fix only proven defects there.

Start with `CONTRIBUTING.md` (formatting, analysis, lint), `TODO.md` (open
work and priorities) and `docs/project/status.md` (what is ready);
`docs/project/decisions.md` lists what jrp has already decided. The pages written
for contributors and agents are in `docs/develop/`: build commands in
`docs/develop/build.md`, the implementation map in
`docs/develop/architecture.md`, tests in `docs/develop/testing.md`, the
plug-in rules in `docs/develop/plugin-contract.md` and the procedures in
`docs/develop/workflow.md`. User behaviour is described in `docs/use/`: the
command line in `docs/use/command-line.md`, configurations in
`docs/use/configuration.md` and the plug-ins in `docs/use/plugins.md`. Open plans are in `docs/project/plans/`;
the plug-in direction is `docs/project/plans/2026-10-21-plugin-formats.md`. How the
documentation is organised and written, for agents too, is in
`docs/develop/documentation.md`.

## Taking on work

Follow `docs/develop/workflow.md`. In short:

- Tell a review, local host testing and a change apart
  ([kinds of task](docs/develop/workflow.md#kinds-of-task)). A review is
  read-only: no branch, commit or GitHub post unless the person asks, except
  the independent review of an open pull request. Host testing records what
  was observed. Only a change gets a branch and the rules below.
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

The procedures, for any agent, are these sections of the workflow guide.
Claude Code sessions reach the same sections through two repository skills,
`todo-item` (start, resume, hand over or finish an item) and `review-change`
(the independent review), which only link to them; the link check covers
`.claude/skills/`, so a renamed section fails in pre-commit.

| Task | Section |
| --- | --- |
| Decide whether this is a review, host testing or a change | [Kinds of task](docs/develop/workflow.md#kinds-of-task) |
| Start an item | [Before you start](docs/develop/workflow.md#before-you-start), then [while working](docs/develop/workflow.md#while-working) |
| Continue someone else's branch | [Picking up someone else's work](docs/develop/workflow.md#picking-up-someone-elses-work) |
| Stop or hand over | [Handoff note](docs/develop/workflow.md#handoff-note) |
| Finish | [Verification](docs/develop/workflow.md#verification-for-each-kind-of-change), [definition of done](docs/develop/workflow.md#definition-of-done) |
| Review a change | [Independent review](docs/develop/workflow.md#independent-review) |

## Read only what the task needs

`TODO.md`, `docs/develop/testing.md` and the plug-in contract are long
reference pages: go to the section your task needs instead of reading them
whole.

| Task | Read |
| --- | --- |
| Choose or claim work | [Execution priorities](TODO.md#execution-priorities), then search `TODO.md` for the item's ID |
| Find the code and its tests | [Change and test map](docs/develop/architecture.md#change-and-test-map) |
| Decide what to run before pushing | [Verification](docs/develop/workflow.md#verification-for-each-kind-of-change) |
| Build | [Sanitizer build](docs/develop/build.md#sanitizer-build-for-changes); plug-in SDKs under [plug-ins](docs/develop/build.md#plug-ins) |
| Understand a native test | [Native production tests](docs/develop/testing.md#native-production-tests) |
| Change or debug a plug-in | The [adapter contract](docs/develop/plugin-contract.md) section for the behaviour, [plug-in comparisons](docs/develop/testing.md#plug-in-comparisons-plug-2) and the format's own [tests](docs/develop/testing.md#plug-in-format-tests) |
| A load-timing test failed or skipped | [Load timing](docs/develop/load-timing.md) |
| A CI run failed | [Evidence from failed CI runs](docs/develop/testing.md#evidence-from-failed-ci-runs), [intermittent failures](docs/develop/testing.md#intermittent-failures) |
| Why CI ran or skipped a job | [CI triggers](docs/develop/testing.md#ci-triggers) |
| FFT backend or a speed claim | [Selection and contract](docs/develop/fft-backends.md#selection-and-contract); the evidence on [measurements](docs/develop/fft-measurements.md) |
| Write host-test steps or record results | [Every report](docs/develop/host-testing.md#every-report) and the host's section of [host testing](docs/develop/host-testing.md) |
| Candidates and preview packages | [Releasing](docs/develop/releasing.md#candidates) |
| Change documentation | [One home for each topic](docs/develop/documentation.md#one-home-for-each-topic), [writing rules](docs/develop/documentation.md#writing-rules) |
| What jrp has decided | [Decisions](docs/project/decisions.md) |

## Build and test

Verify in proportion to the change: the
[verification table](docs/develop/workflow.md#verification-for-each-kind-of-change)
says what prose, executable examples, tooling, code, DSP and plug-in changes
each need. A prose-only change needs `pre-commit run --all-files`, not a native
build. Any change to C++, CMake or a test CTest runs uses the sanitizer build
(all builds, presets and plug-in SDKs are in `docs/develop/build.md`):

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCONVOLVER_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --parallel 4
```

The default build needs CMake 3.20+, Python 3.8+ and libsndfile, plus FFTW3
(single precision) on Linux and Windows. On macOS the default FFT is Apple's
Accelerate (FFTW is an option); plug-in builds use built-in Ooura on Linux and
Windows. See [which libraries each build uses](docs/develop/build.md#which-libraries-each-build-uses). On Ubuntu:
`apt-get install cmake libfftw3-dev libsndfile1-dev libclang-rt-18-dev`.
Install the latest stable packaged Clang tools as in `CONTRIBUTING.md`.
Claude Code cloud sessions install these via `.claude/hooks/session-start.sh`.
CI runs Linux (Clang + GCC), macOS (FFTW and Accelerate) and Windows (MSVC, vcpkg),
plus Ooura configurations described in [testing](docs/develop/testing.md).

- Every DSP change must pass the direct double-precision oracle tests in
  `tests/native/engine_tests.cpp`; report the worst error you observed
  (relative to the output peak, as the test prints it).
- `tests/regression/` compiles **historical** functions by slicing
  `historical/` source on text markers. If you edit those files, update the
  matching `tests/regression/*.py` in the same change.
- Run `pre-commit run --all-files` before **every** push, draft commits
  included. It formats the portable C++ with the latest stable clang-format (also
  `python3 scripts/check_format.py --fix`), formats and lints the Python
  with ruff (`ruff.toml`) and runs the other lint checks;
  lint CI fails on any difference. Cloud sessions install it as a Git hook.
  Changed portable C++ sources must also pass the `.clang-tidy` checks
  (commands in `CONTRIBUTING.md`).
- `python3 scripts/check_repository.py` must pass (pre-commit runs it; the
  list of checks is in the [workflow guide](docs/develop/workflow.md#automated-checks)).
- Do not claim something builds or passes unless you ran it.

## Versioning

Full rules: [source versioning](docs/develop/versioning.md).

- `VERSION` is the one `MAJOR.MINOR.PATCH` source for the CMake package, CLI and
  plug-ins; never copy it elsewhere. Leave `VERSION` and tags alone in ordinary
  changes: after a push to `main`, **Version source changes** bumps PATCH and
  tags `vX.Y.Z`. Edit `VERSION` only for a deliberate minor or major step (an
  incompatible API change before 1.0 needs a minor step), and never move a
  published tag.
- Do not change plug-in IDs or state-format versions to match a package
  version. The version helper creates commits and tags even without `--push`,
  so it is **not a dry run**; test versioning with
  `python3 tests/versioning_tests.py`.

## Rules

- Documentation: follow the [documentation guide](docs/develop/documentation.md)
  (readers, kinds of page, writing rules, which page owns each topic). Update
  the owning page when behaviour changes; keep open work and documented
  limitations in [TODO.md](TODO.md). Distinguish tests you ran from configured
  CI coverage, and preserve dated evidence and historical originals.

- Platform optimisation: use each platform's built-in optimisation methods
  behind a portable interface with a portable fallback, adopting each only
  for a measured gain and keeping output the same or within its stated
  bound ([platform optimisation](docs/develop/architecture.md#platform-optimisation)).
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
