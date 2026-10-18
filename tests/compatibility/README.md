# Configuration compatibility corpus

A small, repository-owned set of Convolver configurations, lists and impulse
responses, each rendered through the real CLI and compared with an independent
double-precision direct convolution. CTest runs it as `compatibility.corpus`
whenever the CLI and Python are built.

```sh
python3 tests/compatibility/run_corpus.py build/convolver            # every case
python3 tests/compatibility/run_corpus.py build/convolver bom-crlf   # selected cases
python3 tests/compatibility/generate_fixtures.py /some/new/dir        # inspect the fixtures
```

## Files

- `cases.json` (manifest schema 1) describes every case: the fixture files to
  generate, the input signal, the filter passed to `--filter`, and either the
  expected graph or an expected diagnostic category.
- `generate_fixtures.py` writes a case's tree. IR WAV files come from the integer
  (PCM16, full scale 32768) or float samples in the manifest; configuration text
  is inline, with optional UTF-8 BOM and CRLF line endings; `{root}` in text
  becomes the case directory's absolute path. Inputs are deterministic
  (`noise` with exact float32 values, or `last-frame-impulse`).
- `run_corpus.py CONVOLVER_EXE` generates each case in a temporary directory and
  renders it with every block size in `defaults.block_sizes` (1, 3, 17, 256),
  with latency trimmed and with `--keep-latency`. Frame counts must match exactly;
  samples must agree within `2e-5 * (1 + max(|actual|, |expected|))`.

## How expectations are stated

The expected result is the graph the configuration should produce, written out
by hand in the manifest: effective input/output delays in frames, and per path
the IR file and channel plus ordered `[channel, gain]` input and output routes.
Signed-zero routes appear as gain `-1`; fractional routes as their weight; a
repeated route is listed twice. `frames` is the output length after latency
trimming: input frames plus the longest connected FIR/delay support. The runner
also checks that the reference has no signal after `frames`. Convolver itself is
never used to generate expected audio.

`selected` names the list entry the configuration should resolve to. Today it
is proven by output (entries are chosen to render differently); inspection
reports can check it directly.

Diagnostic cases (`expect.error`) must exit with status 1, print a message
matching the category's pattern in `error_patterns`, and create no output file.

All renders use float WAV or RAW output, so PCM16 dither never affects them.

## Labels and evidence

Every case carries a label and an `evidence` reference:

- `spec-derived`: behavior stated by the [archived 4.4 configuration
  specification](../../docs/historical/convolver-4.4/config.html).
- `current-behavior`: what the portable reader does today where the specification
  is silent or differs (for example unequal IR lengths, BOM/CRLF, path
  separators). These document behavior; they are not claims about other programs.
- `consumer-verified`: tested in a named downstream program. Such a case must
  also record `consumer`, `revision`, `date` and `evidence_type`. There are none
  yet: no downstream program has been tested against these fixtures.

## Platform differences

Path handling is the only intended platform difference. A case may set `only`
(for example `["posix"]`) or `platform_expect` to state a different expectation
on Windows. Today:

- Relative references with `/`, `\` or mixed separators load on every platform.
- Windows drive-letter, UNC and `\`-rooted references are rejected on macOS and
  Linux with an explicit diagnostic (`drive-letter-path`, `unc-path`). On Windows
  a drive-letter path is an ordinary absolute path (`missing-asset` when absent);
  the UNC case runs only on macOS/Linux.

## Speaker labels

Rendered WAV files have plain headers unless `--speaker-map config` is given; the
runner checks that no case gains a channel mask by accident. A case with
`"speaker_map": true` (today `hex-channel-mask`) is rendered once more with the
flag, and the runner reads `dwChannelMask` from the WAVE_FORMAT_EXTENSIBLE header
itself and checks it equals the configuration's mask, with the same samples.

## Adding a case

Keep fixtures tiny and generated; do not copy third-party filters. Run the new
case against the unmodified loader first. When the result differs from what you
expected, decide whether the expectation or the code is wrong and record which,
rather than editing the expectation to match.
