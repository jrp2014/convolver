# Fuzzing

Four libFuzzer harnesses exercise the inputs Convolver reads from users and hosts.
Each one checks invariants and aborts on a broken one; sanitizers catch memory and
undefined-behaviour errors.

| Harness | Input | Checks |
| --- | --- | --- |
| `fuzz_config_text` | One configuration text | Parses or raises a located `ConfigurationError`; accepted results respect the route, delay and path limits. |
| `fuzz_config_list` | Up to eight in-memory files (`f0` to `f7`), split on NUL bytes | List resolution through in-memory sources: cycles, nesting, visit and text limits, every leaf visited. No real files are read. |
| `fuzz_host_state` | Encoded host state | Decodes or raises `invalid_argument`/`length_error`; a decoded state re-encodes to the same bytes. |
| `fuzz_ir_file` | One audio file | Opens through libsndfile and decodes within the 64 Mi sample limit, or raises `runtime_error`. Written to a private temporary directory, removed at exit. |

## Replay on every compiler

The committed inputs in `seeds/<harness>/` and `regressions/<harness>/` run once
through each harness in every normal test build, without libFuzzer:

```sh
ctest --test-dir build -L fuzz --output-on-failure
```

## Run a campaign (Clang)

```sh
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug \
  -DCONVOLVER_SANITIZERS=ON -DCONVOLVER_BUILD_FUZZERS=ON -DCONVOLVER_BUILD_TESTS=OFF \
  -DCONVOLVER_LEGACY_TESTS=OFF -DCONVOLVER_DOCUMENTATION_TESTS=OFF
cmake --build build-fuzz --target fuzz_config_text
mkdir -p corpus && cp tests/fuzz/seeds/config_text/* corpus/
build-fuzz/fuzz_config_text corpus tests/fuzz/regressions/config_text \
  -max_total_time=600 -max_len=65536 -timeout=2 -rss_limit_mb=512
```

Fuzz into a scratch corpus directory, not `seeds/`, so libFuzzer's new inputs do
not end up in the repository. CI (`.github/workflows/fuzz.yml`) runs each target
for 60 seconds with these options, side by side through
`scripts/fuzz_campaigns.sh` (`--seconds 600` runs the same campaigns locally,
into the ignored `fuzz-out/`; `scheduler_tests.py` tests it); a manual run of
the workflow takes a longer
duration per target, and failing inputs are kept as the `fuzz-findings` artifact.

## When a fuzzer finds something

1. Reproduce: `build-fuzz/fuzz_<harness> crash-<hash>`.
2. Minimise: `build-fuzz/fuzz_<harness> -minimize_crash=1 -runs=100000 crash-<hash>`
   (or `-minimize_crash=1 -max_total_time=60`). The smallest result is written as
   `minimized-from-*`.
3. Fix the cause, then commit the minimised input to `regressions/<harness>/` with a
   name saying what it covers, for example `sample-rate-out-of-range.wav`. The replay
   test then guards it on every compiler.

## Seeds

`make_seeds.py` regenerates the configuration, list and IR seeds from the
[compatibility corpus](../compatibility/README.md). The host-state seeds
(`identity`, `bypass`, `matrix`, and the version-2 `failed` and `failed-bypass`)
come from the C++ state encoder and are committed as they are.
