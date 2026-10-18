# Current review validation

Checked 18 October 2026 against source baseline
`5df874e42ff7c64ca4d8bc0e4f2a1f642ce46699` (portable 0.2.0), with the review/documentation
refresh in the working tree. No production source, build settings or test logic
changed during this refresh. See [REVIEW.md](../../REVIEW.md) for conclusions.

## Fresh local checks

Apple Silicon, AppleClang 21.0.0.21000334, Ninja presets, Debug with ASan/UBSan:

| Configuration | Dependencies | Result |
| --- | --- | --- |
| `debug-sanitizers` | Native FFTW `fftw-3.3.11-neon`, libsndfile 1.2.2 | 17/17 CTest entries passed |
| `accelerate-debug` | System Accelerate/vDSP, libsndfile 1.2.2 | 17/17 CTest entries passed |
| Cross-backend CLI comparison | Both preceding executables | 16 cases / 213,324 samples passed; max absolute error 7.450580596923828e-7, RMS error 8.353387866368425e-8 |

The comparison uses tolerance `2e-5 * (1 + max(abs(a), abs(b)))`. Each backend's
own native suites also use independent direct DFT/time-domain references.
The 17 entries include the installed/relocated external consumer and historical
website preservation. This was correctness validation, not a performance run.

Reproduction, with CMake/Ninja/compiler and dependencies available:

```sh
cmake --preset debug-sanitizers
cmake --build --preset debug-sanitizers
ctest --preset debug-sanitizers
cmake --preset accelerate-debug
cmake --build --preset accelerate-debug
ctest --preset accelerate-debug
python3 tests/native/backend_equivalence.py \
  build/presets/debug-sanitizers/convolver \
  build/presets/accelerate-debug/convolver
```

This machine used separate temporary FFTW-NEON and libsndfile install prefixes
via `CMAKE_PREFIX_PATH`; FFTW config-package discovery was disabled with
`CMAKE_DISABLE_FIND_PACKAGE_FFTW3f=ON` to select the intended NEON installation
through the project's finder. `convolver --version` confirmed the linked versions
above. These paths are machine-local, not a required project configuration.

The first local invocation incorrectly scoped Ninja's PATH to the configure
command. Sixteen entries passed in each build, but the consumer subprocess could
not locate Ninja. Exporting PATH for the full run resolved that invocation error;
both complete 17-entry reruns passed without a repository code change.

## Baseline remote CI

Both workflows completed successfully for exactly the source SHA above:

- [Native and historical tests, run 39158371574](https://github.com/jrp2014/convolver/actions/runs/39158371574):
  Linux Clang Debug/sanitizers, Linux GCC Release, macOS Clang Debug/sanitizers,
  and Windows MSVC. The macOS job also checks Accelerate and cross-backend output;
  the matrix includes core-only/external consumers.
- [VST3 compatibility, run 39158371520](https://github.com/jrp2014/convolver/actions/runs/39158371520):
  Linux, macOS and Windows builds, SDK-interface/preset checks and the SDK validator
  loading the actual bundle.

These are seven successful baseline jobs. They are not a claim that a later
commit has passed; consult its own workflow runs. The local refresh did not
repeat the optional VST3 build: VST3 evidence here is the baseline's remote CI.
Commercial DAWs, hard realtime deadlines and signed/notarized installers were
not validated by these checks.
