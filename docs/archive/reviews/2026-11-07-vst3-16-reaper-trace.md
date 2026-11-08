<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# VST3-16 REAPER latency trace, 7 November 2026

> **Dated record.** jrp's results from tracing a Latency change in REAPER
> with the candidate of PR #134 (the trace build, `d7d16d2`), posted in the
> project thread on 8 November 2026 at 00:00 UTC and copied here verbatim
> below the line, except that links to files on jrp's Mac (the trace,
> renders, notes and scripts, which are not in this repository) are shown
> as file names. The trace build was replaced in the same pull request by
> the fix: a new Latency now plays at once, before the host is asked to
> restart.

---

# VST3-16: REAPER latency trace, 7 November 2026

The four measured renders have **zero-frame offsets for every impulse on both
channels**. The selected latency does not take effect immediately: REAPER
acknowledges the request but keeps the existing processor and its latency until
rendering or an Offline/Online cycle. Ordinary transport play/stop did not apply
the pending change. This is a host-interaction and usability finding; this run
did not reproduce a 768-frame compensation error.

Trace (`trace.txt`) · Notes (`notes.txt`) · Stereo measurements (`measurements.json`) ·
Original walkthrough (`walkthrough-original.md`) · Helper (`impulse.py`) ·
Impulse WAV (`impulse.wav`) · Provenance (`provenance.json`)

## Candidate and setup

- [Candidate run 42420151758](https://github.com/jrp2014/convolver/actions/runs/42420151758):
  successful, including all four platform candidate jobs and collection.
- PR #134 head: `d7d16d2e35c388063cccac8f76cd1604ac8bd4ee`.
- Actual artifact source: `ab29349555b45cfe793cb1025aa6fcdf7ac5aa76`, a merge
  commit whose second parent is that PR head. The walkthrough's expectation
  that `source_commit` starts with `d7d16d2` is incorrect for this artifact.
- Convolver 0.12.0, Accelerate, baseline CPU kernels, VST3 SDK 3.8.1_build_84.
- Package SHA-256:
  `5f76a35d2eaa161509f652dece8fbc28072687c76e965d18937f3569fa6c1a1c`.
  All four manifest asset hashes matched. The extracted and installed VST3
  bundle passed strict/deep code-signature verification; this does not establish
  Developer ID signing or notarization.
- Installed executable SHA-256:
  `d16271289fc779112a159c584898af4ea8a56d1cde29ed1b64a9122501f5ab85`.
- REAPER 7.85/macOS-arm64, process 73208; macOS 27.2 build 26B123.
  Project and device 48000 Hz; device blocks 512 frames.
- Supplied Python helper generated a four-second, stereo, 24-bit WAV. Both
  channels were independently checked for exactly four nonzero samples, each
  8388607, at frames 24000, 72000, 120000 and 168000.
- Two stereo tracks, **reference** and **convolver**, with the same file at zero.
  The latter used the candidate's `identity-48000-stereo/filter.vstpreset`.
  Track/item/take gains unity, pan centred, no item fades; plug-in Gain 0 dB,
  Mix 100%, bypass off. Master gain was 0.125 for comfortable monitoring of two
  full-scale impulse tracks. Selected-track stems bypass that master gain; the
  reference peak was independently verified as 0.9999998807907104.
- Render source **Selected tracks (stems)**, both tracks selected; Entire
  project; 48000 Hz, stereo, WAV 32 bit FP; normalization and dither off; Tail
  enabled at 100 ms. Each valid render has 196800 frames (4.1 seconds).

## Results by step

REAPER's displayed PDC pair is reproduced below as observed. Separate API
snapshots record the plug-in PDC, chain reported latency and chain actual
latency; these agree with the UI. The actual chain figure may be rounded to the
host's block size, so 256/512 does not itself indicate a compensation error.

| Step | Convolver latency row | REAPER display | Result |
| --- | --- | --- | --- |
| 256 baseline | `adds 5.3 ms` | `256/512 spls` | All impulses aligned |
| Choose 1024 while stopped; wait 5 s | `adds 21.3 ms, not in use yet` | `256/512 spls` | Request remains pending |
| Render after that change | `adds 21.3 ms` | `1024/1024 spls` | All impulses aligned; rendering applied the choice |
| Play for 5 s, then stop | `adds 21.3 ms` | `1024/1024 spls` | Remains at 1024 |
| Choose 256 during playback; wait at least 5 s | `adds 5.3 ms, not in use yet` | `1024/1024 spls` | Still playing; choice remains pending |
| Stop after the live change | `adds 5.3 ms, not in use yet` | `1024/1024 spls` | Stop alone did not apply it |
| Render after returning to 256 | Choice 256; API PDC 256 | Chain actual 512 | All impulses aligned; rendering applied 256 |
| Choose 1024 stopped; wait 5 s | `adds 21.3 ms, not in use yet` | `256/512 spls` | Pending again |
| FX Offline for at least 1 s, then Online | `adds 21.3 ms` | `1024/1024 spls` | Choice applied; subsequent impulses aligned |

The 256-after-live-change render was added because the walkthrough requests
four measurement outputs but explicitly lists only three renders. It also
checks the walkthrough's stated reverse-direction acceptance criterion.

| Render prefix | Offsets, left and right | Largest sample error versus reference |
| --- | --- | --- |
| `r256` | `[0, 0, 0, 0]` on each channel | 5.96046448e-08 |
| `r1024stopped` | `[0, 0, 0, 0]` on each channel | 1.19209290e-07 |
| `r256playing` | `[0, 0, 0, 0]` on each channel | 5.96046448e-08 |
| `r1024online` | `[0, 0, 0, 0]` on each channel | 1.19209290e-07 |

Each `PREFIX-measure.txt` holds the supplied helper's output.
`scripts/verify_renders.py` additionally checks both channels, actual WAV rate,
format, length, finite samples, expected reference positions, full-scale peaks
and sample errors. No alignment correction or rescaling was applied. These are
render checks, not recordings of the live transition or listening judgements.

## What the trace establishes

The clean stopped change at 23:41:27 and the live change at 23:44:23 follow the
same pattern:

1. The processor records the requested latency while the running value remains
   unchanged.
2. The controller calls `restartComponent(kLatencyChanged)` on the main thread.
3. REAPER immediately calls `getLatencySamples()`. Convolver returns the actual
   running value: 256 for the first change, 1024 for the reverse change.
4. `restartComponent` returns `kResultOk`. There is no immediate
   `setActive(false)`/`setActive(true)` cycle associated with that request.
5. Rendering later deactivates/reconfigures/reactivates the processor. The new
   latency is then returned and correctly compensated in the measured output.
6. Offline/Online also applies the change. It replaces VST3 instance #1 with #2;
   the new instance restores the filter and chosen latency.

`trace.txt` contains 209 lines from this process, including setup and repeated
attempts, rather than an arbitrary last-400-lines cut from several hosts. There
were no reported trace-buffer overflows. The original trace records only within
its two-minute windows; its absence outside those windows proves nothing.

An important qualification to the walkthrough's proposed decision tree: the
[pinned VST3 SDK's `kLatencyChanged` documentation](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/vst/ivsteditcontroller.h)
describes host deactivation/reactivation followed by reading the new latency.
Convolver's current implementation follows that expectation. This REAPER run
did not perform that cycle immediately. The trace therefore demonstrates a
compatibility gap; it does not prove that returning the old running latency is
itself a plug-in contract violation.

Do **not** fix this by returning the selected value while processing still uses
the old one: that would make reported delay disagree with the actual stream.
A compatibility change must coordinate processor preparation/application with
host latency notification. Its acceptance test must include both live directions
as well as these offline renders. This investigation changed no DSP code.

## UI and UX follow-up

### Latency feedback: highest priority

Keep the chosen value and the active value explicit while a change is pending.
For example:

> In use: 256 frames (5.3 ms). Requested: 1024 frames (21.3 ms).
> Waiting for the host to apply the change.

The current “not in use yet” suffix is accurate but incomplete: it does not say
which delay is still active or how to apply the choice in this host. The help
text's implication that the host will restart it at once is not borne out here.
For this tested REAPER version, Offline/Online is a demonstrated way to apply it;
ordinary transport stop/start is not. Do not turn that into a claim about every
host. Improving this feedback is worthwhile even though these renders align.

### Sample-rate presentation

The current integer formatting is deliberate plain-number output, not locale
formatting: `portable/host.cpp::host_rate_text()` uses `std::to_string(rate) +
" Hz"` for whole rates, and `portable/host_report.cpp` constructs the filter rate
similarly. The macOS editor displays the report text.

Recommended display: **48 kHz**, **44.1 kHz**, with the locale's decimal separator
where appropriate; alternatively, locale-formatted **48,000 Hz**. Keep exact
machine-readable values and `.cfg` syntax unchanged. Mismatch messages and
expanded diagnostics must retain enough precision to distinguish fractional
host rates. Do not blindly localize the report's raw `sample rate` field: the
editor also parses that field numerically to calculate latency in milliseconds.
A shared presentation helper, separate from raw data, avoids inconsistent labels.
No number-formatting change was made in this test.

### Filter name and extension

jrp reported that an extensionless filter name conceals whether the source was a
configuration or a sound file. Code inspection narrows the issue:

- Direct-file reports already use the full basename of the accepted path,
  retaining `.cfg` or `.wav` (`portable/host_report.cpp::file_name()` and
  `host_report()`). No general extension-stripping defect was established.
- Preset/project restoration can instead use a saved display name, or
  `From the saved project or preset`. VST3 can substitute the controller's saved
  name when its filter identity matches. That name alone does not establish an
  original source type. This test loaded an embedded identity preset.

Show a friendly name separately from source information. For a direct file,
keep its full filename and extension visible. For embedded data, identify the
preset/project source, retaining an original filename/type only if known; do not
invent an extension or imply a missing original file is still required. A manual
reproduction of the particular extensionless direct-file case remains open.

## Test automation qualifications

- REAPER's last-settings render command initially produced a master mix, then
  silent stems (including the reference). Those files are excluded and retained
  in `invalid-automation-attempts/`. All accepted measurements use the actual
  render dialog with its source/format verified; the stopped transition was
  repeated from a measured 256-frame baseline after correcting the route.
- The four-second clip initially stopped before the five-second observation.
  Those observations are excluded. For the accepted live checks, a 0–4-second
  loop kept playback running; a recorded `playState=1` verifies it was still
  running after the wait. Looping affects playback, not the entire-project
  stem-render bounds. Repeat was disabled at the end.
- ReaScript launches return before the script necessarily finishes. A missing
  immediate snapshot was followed up, and the completed snapshot records
  playback at 1024 with a pending 256 choice. It was not counted as a plug-in
  failure. Future automation should wait for completion files, not a shell exit.
- REAPER's supported ReaScript API was used for project preparation, read-only
  PDC snapshots, transport and Offline/Online. Latency was changed through
  Convolver's own native menu. Rendering used the verified host dialog.
  [API reference](https://www.reaper.fm/sdk/reascript/reascripthelp.html).
- Visual values were read through accessibility. No screenshot or audible-glitch
  pass is claimed. The test was on this named Mac and host only.

## State left for reuse

Everything is under `~/vst3-16` or the existing user-local plug-in directory.
The user saved the previous crossover project before installation. No system
files, repository files, plug-in IDs or state formats were changed.

- `latency.rpp`: final saved state, identity filter, 1024 frames, playback stopped,
  repeat off; master monitoring gain 0.125.
- `latency-baseline.rpp`: saved 256-frame starting project.
- `impulse.py`, `impulse.wav`, all valid stem renders, measurements, UI snapshots,
  scripts, logs and original instructions are retained.
- The installed VST3 remains the 0.12.0 trace candidate. The original 0.10.13
  bundle is retained intact as `uninstalled-Convolver.vst3`; a second verified
  copy is `previous-Convolver.vst3`. Close plug-in hosts before restoring it.
- Nothing was committed, pushed or posted to a PR.
