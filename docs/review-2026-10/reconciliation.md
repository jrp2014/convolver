# Review branch reconciliation and historical recovery

This is the record of the initial correctness merge. The subsequent owner-authorized
[feature recovery](archive-recovery.md) supersedes the deferral of `convolverPipe`
and its file-descriptor helpers below.

Prepared 17 October 2026 and approved by the owner for a local commit and merge.
This report records the pre-merge comparison of review branch `e380056` and
main `33ed4b8`. Publishing to GitHub is not authorized.
The earlier local fixes remain separately preserved under `source/convolver/`.

## Branch versus the earlier local fixes

| Area | Review branch as received | Prepared result |
| --- | --- | --- |
| Padded IR partition stride | Fixed | Retained; direct-convolution oracle covers awkward lengths and partition counts |
| Input delay overwriting FFT history | Fixed with a separate small ring | Retained this implementation instead of the earlier local full-partition delay buffer; mixed per-channel delays and flush tested |
| FastArray copy overread | Copies the smaller byte count | Retained; earlier local code copied the source and zeroed padding. The branch bounds both sides, and FFTW ignores the extra real-buffer padding |
| Empty IR rejection | Fixed missing throw | Retained |
| Per-path output gain bounds | Fixed | Retained; unequal route counts tested |
| Complex FFT multiplication conditioning | Missing local fix | Restored direct real/imaginary products; `(10000 + 0.0001i)^2` had lost its imaginary component |
| PCM24 and reduced-valid-bit PCM32 | Missing local fixes | Restored sign extension, unsigned byte assembly and left-aligned valid bits on output; existing quantization convention retained |
| VST frame transfer | Missing local fixes | Restored all-inputs-before-outputs, converter-based additive/double handling and byte counts, and silence for disconnected inputs; preserves the branch's small delay ring |
| Negative-zero routes | Additional branch fix | Retained and tested for both input/output routes |
| Dither quantum | Additional branch fix | Retained; constants checked against quantizer scale |
| Convolution list display | Additional branch fix | Retained; all entries and selected markers tested |
| Ooura tail normalization | Additional branch fix | Retained; full/tail round trips use the bundled Ooura FFT |

The old buffer overread is undefined behaviour even when the FFT ignores the
extra values; it is not harmless. The partition-gap defect occurs when padding
changes the partition stride, rather than for every multi-partition filter.
These qualify the corresponding wording in the original branch review.

## Historical source lineage

`homes.zip` SHA-256:
`2b5cad28adcf5d8df21d6c9139941f4ab7607d2225ea29773642ee2beab74771`.
It contains 5,506 entries including macOS metadata, with 503,768,512 compressed
file bytes. ZIP/filesystem dates were not used to choose a version.

Paths below are relative to the archive directory
`homes/DOMAIN=JOHNPAVEL/jrp/My Documents/Visual Studio 2008/Projects/convolver/`.

| Source | Embedded version evidence | Conclusion |
| --- | --- | --- |
| `convolver4-3src.zip`, `Copy of convolver4-3src.zip` | Resources and installer metadata say 4.3 | 183 selected source/project files match between copies. The first ZIP hash also matches the previously downloaded 4.3 archive |
| `convolver.zip`, loose `convolver-/` | WMP, DirectShow, Pipe resources and installer say 4.4 | 191 selected source/project files match byte for byte; corroborated 4.4 sources recovered |
| Loose `convolver/` | Resources/installer say 4.5; VST vendor version 4500 | Later labelled snapshot; a stable 4.5 release is not established |
| `convolver4-5pre1.zip` and five named 4.3/4.4 binary archives | ZIP names; installer-only contents | No source inside these six archives; not used to overwrite source or establish runtime correctness |

[Machine-readable provenance](homes-provenance.json) records archive/member
hashes, embedded version lines, selected source comparison results and exclusions.
Version identifiers in the checkout are deliberately unchanged: this is a
selective recovery onto the reviewed 4.3-derived tree, not a complete 4.4/4.5 import.

## Selected historical improvements

- **4.4 latency display:** `ChannelPaths::DisplayChannelPaths` reports the
  half-partition sample count alongside seconds. This makes the processing
  latency visible without changing DSP behaviour. The original labels call
  these samples “taps”.
- **4.5 error classification:** both configuration readers check `bad()` before
  `fail()`. Since `fail()` also returns true for `badbit`, the earlier order hid
  fatal I/O diagnostics behind syntax-error messages. Eight tests exercise the
  actual ordered clauses with standard stream states. This is a narrow change;
  it does not redesign the existing EOF handling.

These specific changes preserve the review branch's algorithm and routing fixes.
No instructions embedded in archived documents were treated as user instructions.

## Useful historical work deferred

| Candidate | Why it is not included in the prepared code |
| --- | --- |
| `convolverPipe` | Useful direction for modern command-line integration, but zero `nFilterLengths` constructs rather than throws an exception, can index empty vectors, reports caught failures as success, and does not flush the convolution tail at EOF |
| File-descriptor wrapper and format lookup | Supporting code for Pipe; no current consumer. Its RAW bit test also mistakes MAT4/PAF for RAW; a temporary extraction test confirmed the error and the proper type-mask fix. Restore together with a tested pipe tool rather than add unused API and build dependencies now |
| CMD's added final silence block | Recognizes the real missing-tail problem, but appends a fixed block rather than deriving the exact tail/latency requirement; non-libsndfile path also contains incompatible buffer/call expressions. Needs a separate offline-rendering fix and exact output-length tests |
| 4.5 sample format changes | Disables both 64-bit IEEE-float registrations; retain existing support and the newly tested double-sample adapter |
| Visual Studio 2008/project/installer changes | Machine/toolchain churn, changed installer GUIDs and duplicate source-control solution sections do not establish a portable or working build |
| Bundled FFTW/libsndfile binaries | FFTW header change is revision metadata; binary/header/config version claims differ. libsndfile DLL moves from 1.0.16 to 1.0.17 but the header text is unchanged. Do not import historical DLLs as a dependency upgrade |
| Additional media | No compelling regression fixture: the new stereo “2-frame” Dirac file contains three frames and a zero-frame `fact` chunk. Other media/caches lack a clear role in these fixes |

Archive contents remain available in the owner's original ZIP; installers,
SDKs, DLLs, build logs, caches and unrelated media were not copied into the repo.

## Validation and limits

Run from the repository root:

```sh
python3 tests/regression/run_all.py
```

The canonical runner targets `convolver/`; the earlier `tests/*.py` harnesses
still target the preserved `source/convolver/` snapshot and are not the merge gate.
The new tests require Python 3, Clang and sanitizer runtimes, without NumPy.

- Core: **444 checks**, maximum absolute error **4.8494e-08** against independent
  double-precision time-domain convolution. Includes random and impulse inputs,
  mono/matrix routes, unity copies, partition counts 1/2/3/4/8, awkward filter
  lengths, chunk boundaries, mixed input/output delays and flush-to-silence.
- Host conversion: **44 debug/release cases**, including signed PCM, packed
  valid bits, current-frame aliases, additive output, double samples, mixed
  delays and disconnected-input history draining. Before restoration, 36 of
  these cases failed on the review branch.
- Gain: **two route-count cases**, checking estimated and peak gains.
- Branch additions: **four groups** covering routes, list display, dither
  constants and actual Ooura full/tail round trips. All four fail on pre-fix
  commit `61592a3` and pass on the prepared code.
- Historical diagnostics: **eight cases**; the received branch misclassified
  `badbit`, and the recovered ordering passes.
- The branch's independent NumPy model was also run: corrected scenarios have
  maximum relative errors from **4.3e-16 to 6.4e-16**. It is supplemental evidence,
  not a compiled production-code test or an assertion-based test gate.

All C++ harnesses use AddressSanitizer and UndefinedBehaviorSanitizer with
sanitizer failures fatal. Core tests compile actual production DSP bodies but
substitute a portable DFT for FFTW and stub Windows/file boundaries. Host tests
compile actual adapters but substitute checked `std::floor` at the Boost boundary.
Ooura tests exercise real serial FFT arithmetic, not the entire alternate engine.

**Not established:** a native Windows/ATL/DirectShow/VST build; actual FFTW DLL
integration; real-time performance; arbitrary buffer overlap that overwrites
future input frames; nonzero noise-shaping behaviour; the full 32-valid-bit PCM
specialization. Existing alternate-backend macro inconsistencies and offline
output-tail handling remain separate work. The historical sources do not supply
an already-correct replacement for those paths.

## Approved integration scope

The owner approved the following local integration: commit the three production-file edits,
README/provenance/report, and `tests/regression/` on `fix/review-2026-10`, then
merge that branch into local `main` and repeat the regression suite. Publishing
these commits to GitHub is a separate action. The original untracked source,
archives, reports and old harnesses are outside the proposed commit.
