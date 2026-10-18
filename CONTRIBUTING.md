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

## Scope and versions

The portable CMake package is versioned separately from archived releases. The
initial static 0.1 API can evolve between minor versions; no stable binary ABI is
promised. Installed dependency versions remain externally managed. A future
binary-release milestone should pin baselines and define dependency bundling.

Compatibility changes need fixtures for the affected config/path or file
semantics. PCM16 quantization and dither follow the documented policy in
[the portable guide](docs/portable-core.md); PCM24/PCM32 use libsndfile's
conversion without dither. Do not silently change either behavior in a
build-system or formatting patch, and keep noise shaping a separate, explicit option.

For host changes, run the optional SDK build and its validator/adapter/preset
tests as described in [the VST3 guide](docs/vst3.md). Report actual DAW tests
separately from SDK validation.
