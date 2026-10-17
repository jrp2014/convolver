# Guidance for coding agents and contributors

Convolver is a 2005–2006 Windows audio convolution engine (GPL-2.0-or-later)
with several front ends: DirectShow filter, WMP DSP plug-in, VST 2 plug-in,
`convolverCMD` (file to file) and `convolverPipe` (stdin to stdout). This file
says how to work on it safely. Keep it short and update it when a rule changes.

## Layout

- `convolver/convolution/` – the shared DSP engine. This is the code that matters.
  `filter.*` loads and partitions impulse responses, `convolution.*` runs
  uniformly partitioned overlap-save convolution, `channelpaths.*` parses the
  routing part of the `.cfg` format, `sample.h` converts sample formats and moves
  frames between host buffers and the engine.
- `convolver/convolver{CMD,Pipe,Filter,WMP,VST}/` – front ends. Windows only.
- `convolver/Common/` – Microsoft DirectX sample framework (DXUT). Not ours; do not edit.
- `convolver/Media/` – sample WAVs and impulse responses used for manual testing.
- `tests/regression/` – the only tests that run anywhere today.
- `docs/` – review and recovery records, the pipe guide, and the archived 4.4 website.

## Building and testing

- The Windows targets need Visual Studio 2005-era toolchains, ATL, the DirectX
  and Windows Media SDKs, FFTW and libsndfile DLLs. Nothing builds them in CI.
  Do not claim a change "builds" unless you compiled it.
- Run `python3 tests/regression/run_all.py` before and after any engine change.
  It needs Python 3 and Clang with the AddressSanitizer/UBSan runtimes (on
  Ubuntu: `apt-get install clang libclang-rt-18-dev`; Claude Code cloud sessions
  install it via `.claude/hooks/session-start.sh`). CI runs it on Linux and macOS.
- The suites compile the real production functions by **slicing source files on
  text markers** (for example `// Convolution Constructor`, `\tstatic void
  StoreDelayed(`) and patching them. If you move, rename or reformat code the
  suites slice, update the matching `tests/regression/*.py` in the same change.
  The long-term plan is to replace slicing with a normal library build.
- Numerical changes must be checked against an independent oracle (the suites
  use double-precision time-domain convolution). Report the worst error.

## Rules

- Do not reformat legacy files wholesale; it destroys `git blame` across 20 years
  of history. Format only lines you change, matching the surrounding style
  (tabs, Allman braces).
- Line endings: text is LF in the repo; `.sln`, `.vcproj`, `.rc` are CRLF via
  `.gitattributes`. Files under `docs/historical/` are byte-exact copies with
  recorded hashes; never edit them.
- The `.cfg` configuration format is used by Roon, JRiver and Audiolense. Any
  parser change must keep existing files loading with identical results.
- Real-time paths (`doConvolution`, `doPartitionedConvolution`, `sample.h`
  `GetSample`/`PutSample`) must not allocate, lock or do I/O.
- Third-party code (`Common/`, `fftw/` and `libsndfile/` headers) is vendored;
  prefer updating it from upstream over local patches.
- Keep commits focused, and explain the audible or numerical effect of any DSP
  fix in the commit message.
