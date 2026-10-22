<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Working on portable Convolver

Start with [the implementation map](docs/project-structure.md) for source entry
points, invariants and tests. Coding agents must also follow [AGENTS.md](AGENTS.md).
[Taking on and reviewing work](docs/workflow.md) explains how to claim a TODO
item, leave a handoff note and get a pull request reviewed and merged.
For prose changes, use [the documentation maintenance guide](docs/documentation.md).

Use CMake 3.20+, C++20 and the real FFT/library dependencies. Start with
[the portable guide](docs/portable-core.md) and [package/preset instructions](docs/cmake-package.md).
Keep host APIs and file access outside the DSP core. Preserve the historical
source/documents and their provenance; do not mechanically modernize them when
editing the portable implementation.

For a normal change, build and run the complete CTest configuration. Use a
separate sanitizer build for correctness and Release for measurements. The
core-only preset checks dependency isolation; the package-consumer test checks
installed and source-tree use. Keep independent time-domain/DFT oracles and
meaningful error tolerances when changing DSP code. Preserve latency, tail,
routing and file-protection behavior unless the change explicitly revises them.

## Formatting and analysis

The portable code uses `.clang-format` with LLVM clang-format **18**. Formatting
is part of the [lint checks](#lint-checks): `pre-commit run` reformats C++ files
in place with a pinned clang-format 18, and the lint CI job fails if that
changes anything. `scripts/check_format.py` finds the modern C++ sources by
location (`include/`, `portable/`, `plugins/`, `tests/`), so new adapters and
test folders are covered automatically. It skips vendored code and the
regression harnesses:

```sh
pre-commit run clang-format --all-files   # reformat, with the pinned version
python3 scripts/check_format.py           # check only
python3 scripts/check_format.py --fix     # reformat in place
```

Run one of these before every push, including early draft commits.
`scripts/check_format.sh` still works and runs the same script. The checker
uses `CLANG_FORMAT` if set; otherwise the clang-format installed beside its
Python (pre-commit's pinned 18.1.8), then `clang-format-18`, then
`clang-format`, taking the first that reports version 18. It stops with
installation advice if none does, because other major versions lay out the same
code differently. macOS has no `clang-format-18` by default; use the pre-commit
command, or `python3 -m pip install clang-format==18.1.8` in the lint
environment below. `--fix` exits with status 1 when it changed a file.
Historical directories are also excluded by `.clang-format-ignore`. Formatting
changes should be distinguishable from behavior changes in review.

Presets export `compile_commands.json`. Run the correctness-oriented checks in
`.clang-tidy` against the translation units present in that build:

```sh
clang-tidy-18 -p build/presets/release portable/engine.cpp portable/attenuation.cpp portable/config.cpp portable/config_parse.cpp portable/inspection.cpp portable/inspection_cli.cpp portable/io.cpp portable/main.cpp portable/fft_fftw.cpp portable/host.cpp portable/state.cpp
```

Replace `fft_fftw.cpp` with `fft_accelerate.cpp` or `fft_ooura.cpp` for the
corresponding backend. Match
the analyzer to the compiler/SDK headers: older analyzers may not parse newer
Apple SDKs. CI uses the Linux clang-tidy 18 toolchain. Fix real diagnostics rather
than adding blanket suppressions. These tools are development dependencies;
library consumers need neither tool.

## Lint checks

Use Python 3.12 and Node.js 24 for the same environment as the lint CI job.
Install the pinned runner in an isolated environment:

```sh
python3 -m venv .venv-lint
. .venv-lint/bin/activate
python -m pip install pre-commit==4.5.1
pre-commit run --all-files --show-diff-on-failure
```

On Windows, create the environment with `py -3.12 -m venv .venv-lint` and
activate it in PowerShell with `.venv-lint\Scripts\Activate.ps1`, then run the
same pip and pre-commit commands. Run `pre-commit install` once so the checks,
formatting included, run on staged files before each commit. The first run
downloads the pinned tools; subsequent runs reuse their isolated environments.

The configuration runs clang-format 18 on the portable C++ sources (see
[formatting](#formatting-and-analysis)), ShellCheck, markdownlint-cli2, strict
yamllint, and actionlint with ShellCheck for Bash embedded in GitHub Actions
workflows. Hooks that fix files, such as clang-format, report "files were
modified by this hook"; review and stage the changes, then run again. Claude
Code cloud sessions install the Git hook automatically
(`.claude/hooks/session-start.sh`), so their commits run these checks.
Historical source, recovered documentation and archived review evidence are
excluded. Markdown allows soft-wrapped prose and repeated subheadings under
different parent sections; other diagnostics should be fixed at their source.
These checks are development tools and do not add package runtime dependencies.
Use `pre-commit run --files <paths>` to include new files before staging them;
`--all-files` checks files already tracked by Git. The clang-format hook is the
exception: it always checks every portable C++ source, untracked ones included.

The local hooks also check maintained Markdown links and the repository rules
(TODO IDs, the documentation index, plan folders, paths named in agent
guidance, licence lines and the top-level layout), and run both checkers'
regression tests on every invocation. They use only Python's standard library
and can be run independently:

```sh
python3 scripts/check_doc_links.py
python3 tests/documentation_links_tests.py
python3 scripts/check_repository.py
python3 tests/repository_checks_tests.py
```

[Repository checks](docs/workflow.md#automated-checks) lists each rule and what
to do when one fails.

The link check scans top-level Markdown and maintained guides under `docs/`,
including untracked files. Dated records and archives are excluded as source
pages; links into them must still resolve. It checks local destinations and
supported heading/HTML anchors, not external websites or agreement with code.
See [documentation maintenance](docs/documentation.md#automated-checks) for scope.

Workflows use the latest major release of each action. Dependabot
(`.github/dependabot.yml`) checks weekly and opens one grouped pull request when
an action has a new release; review its release notes for changed inputs before
merging. `ilammy/msvc-dev-cmd` has no Node 24 release yet, so GitHub still runs
it on Node 24 with a deprecation warning.

CI caches pre-commit environments by operating system, architecture, exact Python
and Node versions, and `.pre-commit-config.yaml`. Windows jobs cache vcpkg binary
archives separately for dynamic and static triplets. Their keys include the runner
image, vcpkg revision and workflow dependency recipe; fallback archives are still
checked by vcpkg's ABI matching before reuse. Only dependency archives are cached,
not installed trees, Convolver builds or test results. Dependency installation,
lint and every build/test step run even on a cache hit.

To verify caching, compare two successful runs of the same workflow revision:
the first should save a cache and the next should restore it. For vcpkg, also check
the install log for restored packages rather than relying only on the Actions
cache hit. Branch caches are scoped by GitHub, so the first run on `main` may be
cold after testing a feature branch. Bump the `v1` key prefix to discard a cache
generation if needed; an unavailable cache must only affect build time.

## Scope and versions

The portable CMake package, CLI and VST3 plug-in share `VERSION`. Source changes
on `main` receive automatic patch versions and annotated Git tags; see
[source versioning](docs/versioning.md) for scope, deliberate minor/major bumps
and recovery. The pre-1.0 API can evolve between minor versions; no stable binary
ABI is promised. Archived releases retain their separate history.
Installed dependency versions remain externally managed. A future
binary-release milestone should pin baselines and define dependency bundling.

Compatibility changes need fixtures for the affected config/path or file
semantics. PCM8/16/24/32 quantization, optional dither, noise shaping and valid bits follow
[the portable guide](docs/portable-core.md#integer-formats-dither-and-noise-shaping).
Do not change their audible or numerical behavior in a build-system or formatting
patch; keep optional processing explicit and test exact samples as well as statistics.

For host changes, run the optional SDK build and its validator/adapter/preset
tests, and pluginval if you can, as described in [the VST3 guide](docs/vst3.md). Report actual DAW tests
separately from SDK validation.
