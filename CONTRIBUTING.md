# Working on portable Convolver

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

The portable code uses `.clang-format`; CI checks LLVM clang-format **18**.
Format only these modern sources:

```sh
clang-format-18 -i include/convolver/*.hpp portable/*.hpp portable/*.cpp tests/native/*.cpp tests/consumer/*.cpp plugins/vst3/*.hpp plugins/vst3/*.cpp tests/vst3/*.cpp
clang-format-18 --dry-run --Werror include/convolver/*.hpp portable/*.hpp portable/*.cpp tests/native/*.cpp tests/consumer/*.cpp plugins/vst3/*.hpp plugins/vst3/*.cpp tests/vst3/*.cpp
```

Historical directories are also excluded by `.clang-format-ignore`. Formatting
changes should be distinguishable from behavior changes in review.

Presets export `compile_commands.json`. Run the correctness-oriented checks in
`.clang-tidy` against the translation units present in that build:

```sh
clang-tidy-18 -p build/presets/release portable/engine.cpp portable/config.cpp portable/io.cpp portable/main.cpp portable/fft_fftw.cpp portable/host.cpp portable/state.cpp
```

For an Accelerate build replace `fft_fftw.cpp` with `fft_accelerate.cpp`. Match
the analyzer to the compiler/SDK headers: older analyzers may not parse newer
Apple SDKs. CI uses the Linux clang-tidy 18 toolchain. Fix real diagnostics rather
than adding blanket suppressions. These tools are development dependencies;
library consumers need neither tool.

## Shell, Markdown and YAML checks

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
same pip and pre-commit commands. Optionally run `pre-commit install` to check
staged files automatically before each commit. The first run downloads the
pinned tools; subsequent runs reuse their isolated environments.

The configuration runs ShellCheck, markdownlint-cli2, strict yamllint, and
actionlint with ShellCheck for Bash embedded in GitHub Actions workflows.
Historical source, recovered documentation and archived review evidence are
excluded. Markdown allows soft-wrapped prose and repeated subheadings under
different parent sections; other diagnostics should be fixed at their source.
These checks are development tools and do not add package runtime dependencies.
Use `pre-commit run --files <paths>` to include new files before staging them;
`--all-files` checks files already tracked by Git.

## Scope and versions

The portable CMake package, CLI and VST3 plug-in share `VERSION`. Source changes
on `main` receive automatic patch versions and annotated Git tags; see
[source versioning](docs/versioning.md) for scope, deliberate minor/major bumps
and recovery. The pre-1.0 API can evolve between minor versions; no stable binary
ABI is promised. Archived releases retain their separate history.
Installed dependency versions remain externally managed. A future
binary-release milestone should pin baselines and define dependency bundling.

Compatibility changes need fixtures for the affected config/path or file
semantics. Dither remains a planned audio-compatibility feature: current integer
output rounds/clips without adding dither, including PCM16. Do not silently
change that behavior in a build-system or formatting patch.

For host changes, run the optional SDK build and its validator/adapter/preset
tests as described in [the VST3 guide](docs/vst3.md). Report actual DAW tests
separately from SDK validation.
