<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Working on portable Convolver

Start with [the implementation map](docs/develop/architecture.md) for source entry
points, invariants and tests. Coding agents must also follow [AGENTS.md](AGENTS.md).
[Taking on and reviewing work](docs/develop/workflow.md) explains how to claim a TODO
item, leave a handoff note and get a pull request reviewed and merged.
For prose changes, use [the documentation maintenance guide](docs/develop/documentation.md).

Build with the commands in [build from source](docs/develop/build.md), using the real
FFT and library dependencies; the [library guide](docs/develop/library.md) covers presets and package use.
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

Use the **latest stable packaged releases** of clang-format and clang-tidy
(owner's decision, 24 October 2026). Development builds and release candidates
are not the default. The standalone packages on
[PyPI](https://pypi.org/project/clang-format/) avoid requiring a complete LLVM
installation. A package can lag an upstream LLVM patch release; use the newest
stable package available and report its version.

The `.clang-format` file defines the style. The pre-commit hook installs the
latest stable formatter when its environment is created and reformats portable
C++ in place. CI resolves the current stable package before restoring its cache,
so a new release gets a new environment. Refresh a local hook environment when
updating the tools:

```sh
pre-commit clean
pre-commit run clang-format --all-files
```

Run `pre-commit run --all-files` before every push. A formatter update can change
layout; commit mechanical reformatting separately from behaviour changes.
Historical and vendored sources remain excluded. CI uploads a `lint-fixes`
patch when lint leaves changes, so formatting can also be reviewed and applied
without installing Clang locally.

For manual checks, install or upgrade the standalone tools in the lint
virtual environment below, then activate it:

```sh
python -m pip install --upgrade clang-format clang-tidy
clang-format --version
clang-tidy --version
python scripts/check_format.py
```

The checker honours `CLANG_FORMAT` when set. Otherwise it uses the formatter
beside its Python interpreter (the pre-commit environment), then `clang-format`
on PATH. It rejects development-version suffixes and prints the selected version;
it does not install software or query the network. Installation and CI choose
the latest release, rather than a hard-coded major-version test in the checker.

Presets export `compile_commands.json`. Run the correctness-oriented checks in
`.clang-tidy` against the translation units present in that build:

```sh
clang-tidy -p build/presets/release portable/engine.cpp portable/kernels.cpp portable/kernels_avx2.cpp portable/attenuation.cpp portable/config.cpp portable/config_parse.cpp portable/inspection.cpp portable/inspection_cli.cpp portable/io.cpp portable/main.cpp portable/fft_fftw.cpp portable/host.cpp portable/host_log.cpp portable/state.cpp portable/presets.cpp portable/preset_main.cpp portable/equalizer_apo.cpp portable/equalizer_apo_main.cpp
```

Replace `fft_fftw.cpp` with `fft_accelerate.cpp` or `fft_ooura.cpp` for the
corresponding backend. In a build with `-DCONVOLVER_BUILD_LV2=ON`, add
`plugins/lv2/plugin.cpp` (the [LV2 workflow](.github/workflows/lv2.yml) checks
it). Match
the analyzer to the compiler/SDK headers: older analyzers may not parse newer
Apple SDKs. CI upgrades the standalone clang-tidy package before each analysis
run and prints its version. Fix real diagnostics rather than adding blanket suppressions. These tools are development dependencies;
library consumers need neither tool.

### When a new Clang release turns CI red

Because the tools are not pinned, a new release can fail CI on code that
nobody changed. That is accepted (owner's decision, 24 October 2026); handle
it by which tool failed:

- **clang-format** changes layout only, never behaviour. Apply the
  `lint-fixes` patch from the failed Lint run, or run `pre-commit clean` and
  `pre-commit run clang-format --all-files`, and commit the result on its own
  as `style: apply clang-format X.Y.Z layout`. Do not change `.clang-format`
  to keep the old layout unless jrp asks.
- **clang-tidy** adds or sharpens checks, and `.clang-tidy` treats every
  warning as an error, so a new failure usually points at real code. Fix the
  code in its own commit and say what the diagnostic found. If a diagnostic is
  a false positive, suppress that one line with `// NOLINT(check-name)` and a
  reason; do not turn the check off.

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
downloads the tools; subsequent runs reuse their isolated environments.
Refresh the formatter environment as described above when upgrading.

The configuration runs the latest stable packaged clang-format on the portable C++ sources (see
[formatting](#formatting-and-analysis)), ShellCheck, markdownlint-cli2, strict
yamllint, and actionlint with ShellCheck for Bash embedded in GitHub Actions
workflows. Hooks that fix files, such as clang-format, report "files were
modified by this hook"; review and stage the changes, then run again. Claude
Code cloud sessions update their Clang tools in `~/.cache/convolver-lint/bin/`,
refresh the formatter hook environment and install the Git hook automatically
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
regression tests on every invocation. A further hook checks the
[permanent plug-in identities](docs/develop/plugin-identities.md#changing-the-registry),
and another tests which files let a pull request
[skip the builds](docs/develop/testing.md#documentation-only-pull-requests). They use only Python's standard library
and can be run independently:

```sh
python3 scripts/check_doc_links.py
python3 tests/documentation_links_tests.py
python3 scripts/check_repository.py
python3 tests/repository_checks_tests.py
python3 tests/identities_tests.py
python3 tests/ci_scope_tests.py
```

[Repository checks](docs/develop/workflow.md#automated-checks) lists each rule and what
to do when one fails.

The link check scans top-level Markdown and maintained guides under `docs/`,
including untracked files. Dated records and archives are excluded as source
pages; links into them must still resolve. It checks local destinations and
supported heading/HTML anchors, not external websites or agreement with code.
See [documentation maintenance](docs/develop/documentation.md#automated-checks) for scope.

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
cold after testing a feature branch. Bump a cache key prefix to discard a cache
generation if needed; an unavailable cache must only affect build time.

## Scope and versions

The portable CMake package, CLI and VST3 plug-in share `VERSION`. Source changes
on `main` receive automatic patch versions and annotated Git tags; see
[source versioning](docs/develop/versioning.md) for scope, deliberate minor/major bumps
and recovery. The pre-1.0 API can evolve between minor versions; no stable binary
ABI is promised. Archived releases retain their separate history.
Installed dependency versions remain externally managed. A future
binary-release milestone should pin baselines and define dependency bundling.

Compatibility changes need fixtures for the affected config/path or file
semantics. PCM8/16/24/32 quantization, optional dither, noise shaping and valid bits follow
[the portable guide](docs/use/command-line.md#integer-formats-dither-and-noise-shaping).
Do not change their audible or numerical behavior in a build-system or formatting
patch; keep optional processing explicit and test exact samples as well as statistics.

For host changes, run the optional SDK build and its validator/adapter/preset
tests, and pluginval if you can, as described in [testing](docs/develop/testing.md#pluginval). Report actual DAW tests
separately from SDK validation.
