<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in editor plan: a window to configure, load and debug

Status: written on 24 October 2026 against Convolver `80ac07a` (0.4.45), at
jrp's request ("I think that doing that would be helpful, not least for
debugging"), and revised the same day after jrp asked that the window
configure the plug-in as the old Convolver did, modernised in line with what
the engine can do now, and again on 25 October as jrp answered its
questions. Gain and Mix, in the shared code and as the Audio Unit's
parameters (AU-7), were implemented on 25 October 2026 (PR #80; the
[contract's settings](../../develop/plugin-contract.md#settings)), Audio Unit
first because jrp starts testing in GarageBand, and kept simple at jrp's
request; the rest is not implemented. jrp's choices are recorded in
[decisions for jrp](#decisions-for-jrp) and the
[decisions log](../decisions.md#direction): native views, with VSTGUI as a
stop-gap for the Linux VST3 window; each format's own way of saving the
settings; and a first preview that waits for the settings and the windows
of VST3 and the Audio Unit, which joins that preview. What remains for jrp comes up as each phase proposes it: the Gain
range, new parameter IDs and LV2 port symbols. Open work is tracked as PLUG-9,
PLUG-10, PLUG-11, VST3-3, VST3-4, VST3-11 to VST3-13, AU-6, AU-7, LV2-6 and
LV2-7 in [TODO](../../../TODO.md#plug-ins-vst3-lv2-and-au); HOST-2 is the umbrella item.
The rules every adapter follows stay in the
[plug-in adapter contract](../../develop/plugin-contract.md); where this plan
and the contract seem to disagree, the contract wins until a change to it is
decided and merged in its own pull request.

For jrp, deciding how to build the editor and what it configures, and for
whoever then implements one of its phases.

**In short:**

- **What the window is for.** One window, the same in every format, that
  configures the plug-in as the old Convolver's windows did and explains what
  it is doing. Configure: the filter (`.cfg`, list or sound file, chosen,
  dropped on the window or picked from recent files, and reloaded when the
  file changes), the level (gain, with the old "optimum attenuation" estimate
  and the no-clip bound as one-click modes), the latency (the modern form of
  the old partitions setting) and the wet/dry mix of the old
  wrapper's "Effect level". Explain: the Status and the full explanation of
  any failure or fault, what is loaded and how the host started it, an output
  peak and clip count, the processing load, and the Convolver log, with Copy
  diagnostics for reports.
- **Modernised, not copied.** Settings that the engine made obsolete go: FFTW
  "tuning rigour" (released plug-ins use Ooura or Accelerate, which have no
  planner; a plug-in built with FFTW gets an optional planning choice that
  states its small measured gain, PLUG-11), and dither and noise shaping (plug-ins hand floating-point audio to
  the host, and dither belongs once, at the host's final conversion to
  integer). The rest become host parameters, so they also work
  from hosts' generic controls, automation and presets, even before the
  window exists.
- **Framework: native views, no framework** (decided). One AppKit view on
  macOS serves VST3 and the Audio Unit (and an LV2 Cocoa UI later); a Win32
  view serves VST3 on Windows; the Linux VST3 window uses VSTGUI as a
  stop-gap (jrp's choice). Native controls give screen readers, keyboard use, dark mode and
  scaling without work of our own.
- **Threads.** The window talks only to the controller side (the VST3 edit
  controller, AU properties and parameters, LV2 UI messages), never to the
  processor or the audio thread. Settings travel as ordinary host
  parameters; filter loads as requests handled like a state restore; gain
  estimates on a worker; meters as counters the audio thread updates without
  locking.
- **Settings first, windows second.** Phase 1 needs no window: the shared
  report and load path (PLUG-9) and the settings in the host layer, state and
  preset tool (PLUG-10), then per format (VST3-3 and VST3-4, AU-7, LV2-7),
  all usable from generic host controls and tested headlessly. The macOS
  window (VST3-11, reused by the Audio Unit in AU-6) follows, because jrp's
  test hosts are on his Mac, then Windows (VST3-12) and the Linux stop-gap
  (VST3-13). The first preview waits for the settings and the windows of
  VST3 and the Audio Unit, which jrp moved into that preview;
  an LV2 UI (LV2-6) only if Linux users ask.
- **Testing.** Gain, mix and latency pass the double-precision oracle tests
  like any DSP change; everything the window shows is built by shared code
  that the existing headless tests check per failure kind; the windows are
  opened in CI by `editorhost`, pluginval's GUI tests, `auval` and an
  accessibility-tree test per platform.

Contents: [what the window does](#what-the-window-does) ·
[what the old Convolver configured](#what-the-old-convolver-configured) ·
[settings](#settings) · [framework choice](#framework-choice) ·
[how the window connects](#how-the-window-connects) ·
[threads](#threads) · [per format](#per-format) ·
[accessibility](#accessibility) · [testing](#testing) ·
[phases and TODO items](#phases-and-todo-items) ·
[risks](#risks-and-open-questions) · [decisions for jrp](#decisions-for-jrp)

## What the window does

The window configures first and explains always. Configuring is what the old
Convolver's windows were for; explaining is what jrp asked for in debugging,
and diagnostics that explain generously ([decisions](../decisions.md#diagnostics)):
today the full explanation of a failure is only in the Convolver log, while
the host shows at most the Status parameter's one line
([status and log](../../develop/plugin-contract.md#status-and-log)).

A sketch, top to bottom in reading and Tab order (sizes and wording to be
settled in VST3-11):

```text
+- Convolver 0.5.0  -  VST3 #2 in REAPER --------------------------------+
| (*) Playing: room.cfg, entry for 48000 Hz, 2 in -> 2 out               |
|     On a failure or fault this banner says what failed, why and what   |
|     to change, in full.                                                |
|                                                                        |
| Filter    [ room.cfg                     v ] [Choose...]  [Reload]     |
|           [x] Reload when the file changes      or drop a file here    |
|                                                                        |
| Level     Gain [ -31.5 ] dB    ( ) Set  (*) Estimate  ( ) No clipping  |
|           Estimate -31.5 dB, no-clipping bound -70.2 dB                |
|           Output peak -3.2 dBFS, no samples above full scale [Reset]   |
|           Mix [====================|] 100 % wet                        |
|                                                                        |
| Latency   [ 256 frames (5.3 ms) v ]    Load: 3 % of each callback      |
|                                                                        |
| > Details: host, filter paths, tail, FFT library, CPU kernels, log     |
|                                                                        |
| [Copy diagnostics]  [Show log file]                         [ ] Bypass |
+------------------------------------------------------------------------+
```

| Part | Shows or does | Source (all on the control side unless stated) |
| --- | --- | --- |
| Title line | Version, format, instance tag (`VST3 #2`, as in the log) and host program, so a screenshot or copied report identifies the instance | `execution_line()` parts, the adapter's log tag |
| Status banner | The `HostStatus` name, an icon and the one-line summary; below it, the full message: `HostFilter::failure()`, the speaker notice, and `fault_message()` for faults, with request numbers. Selectable text. When the status is Not checked yet, it says that the host has not started this instance and that some hosts (Audacity's real-time effects) show a separate copy in the window | `HostFilter`, `host_status()`, `observe()` |
| Filter | The accepted filter's file (or "from the saved project or preset", since state holds no path), with the list entry chosen and why for a list. **Choose...** opens the system file dialog; a file can also be dropped on the window or picked from the recent-files menu. **Reload** reads the file again; **Reload when the file changes** does it automatically while the window is open, for someone refining a filter in DRC, REW or a spreadsheet. Configuration warnings (`inspect_configuration` diagnostics, such as paths that will not travel) are listed under it | `HostFilter::accepted()`, `inspect_configuration()` |
| Level | Gain in dB, typed or with a fine slider, in one of three modes that are the CLI's `--gain-db` choices: **Set** (a fixed value), **Estimate** (the old "Calculate optimum attenuation", recomputed for every filter loaded) and **No clipping** (the guaranteed bound, the old "Peak gain"). Both computed values are always shown. An output peak and a count of samples above full scale since the last reset show whether the gain is right while music plays | [Settings](#settings) |
| Mix | Wet/dry balance, the old wrapper's "Effect level (%)", with the dry signal delayed by the latency so they line up; disabled with a note when input and output counts differ, as bypass then plays silence | [Settings](#settings) |
| Latency | A menu of block sizes in frames and milliseconds at the host rate, the modern form of the old partitions setting (smaller is less delay, more processor load), with the measured load per callback beside it | [Settings](#settings) |
| Details (collapsible) | Host rate, channels, speakers and largest block; filter rate, counts, paths (input to output, gain, delay) and longest filter in taps and seconds; tail; FFT library and version; CPU kernels; the log path or why it cannot be written | `HostLayout`, the accepted `Configuration`, `HostProcessor`, `fft_backend_name()`, `cpu_kernels_name()`, `host_log_path()` |
| Copy diagnostics, Show log file | The whole report and this instance's recent log lines as plain text on the clipboard, ready to paste into a report or to Claude; the log revealed in Finder, Explorer or the file manager | `report_text()`, the log |
| Bypass | The host's bypass (VST3 parameter, AU property, LV2 `enabled` port), changed through the host so automation and undo see it | The format's own bypass |

**Copy diagnostics** is the debugging feature: one plain-text block, the same
in every format and system, made by one shared function, with the version,
format, host, status, full messages, settings, request history of this
instance, host and filter details, FFT backend and kernels, meters, and the
last lines this instance wrote to the log. Its wording reuses the log's
sentences, so a report pasted from the window and a log file never disagree.

**Look.** One column, the system font and the system's own controls,
following light and dark appearance, with group labels down the left as in
the sketch; no bitmaps or custom skins. The status banner uses the system's
accent and warning colours with an icon and the status word, so colour is
never the only signal. The window resizes, and the full message wraps rather
than being cut off.

## What the old Convolver configured

The old windows are preserved in the [archived website](../../historical/convolver-4.4/README.md),
which matched the live convolver.sourceforge.net pages on 17 October 2026:
the VST2 window (`images/vst.ht1.jpg` on the [VST page](../../historical/convolver-4.4/vst.html)),
the Windows Media Player, DirectShow filter and wrapper property pages of
versions 2.3 to 4.4 (`images/index.2.jpg`, `images/usage.2.jpg`,
`images/using_13.jpg` and `images/using_18.jpg`, explained on the
[usage page](../../historical/convolver-4.4/usage.html)), and the 2006
source in `historical/convolverVST/convolverVSTEditor.cpp`. They are
inspiration, not a specification: each old control is kept only where it
still serves a user, in a form that fits today's engine and hosts. Every
setting they offered, and what it becomes:

| Old setting (where) | What it did | Now |
| --- | --- | --- |
| **Filter/config**, **Get config or IR** (all) | Chose a sound file or `.cfg`; the path shown truncated in the middle | **Kept and extended:** the same, plus lists choosing their entry by the host's rate and channels, drag and drop, recent files, Reload and reload on change, and configuration warnings |
| Filter description line (all) | "2 Paths (Stereo to Stereo direct) 44.1kHz 50544 taps Lag: 25272 taps 0.57s Estimated gain: 30dB Peak gain: 70dB" | **Kept:** the status banner and Details, with the gains shown under Level |
| **Attenuation (dB)**, ±200 in the VST2 plug-in, ±1000 in the property pages (all) | Scaled the input by 10^(dB/20); negative to avoid clipping, positive to amplify | **Kept as Gain**, the same sign and meaning as the CLI's `--gain-db` |
| **Calculate optimum attenuation** button (property pages); estimated and peak gain in the description (VST2) | Ran the noise estimate; the peak gain was a guaranteed but "extremely conservative" bound | **Modernised:** the Estimate and No clipping modes, recomputed automatically for each filter, seeded so the estimate is the same every time (the old one varied), plus an output peak and clip count so the user sees the result |
| **Effect level (%)** (wrapper, version 2.3) | Wet/dry mix | **Kept as Mix**, with the dry path latency-aligned |
| **Partitions** / **No of partitions** (all) | Traded latency against processor load and cache use; 0 meant plain overlap-save | **Modernised as Latency:** the engine always uses uniform partitions of B frames, so the choice is B itself, shown as delay in milliseconds, with the measured load beside it instead of the old advice to time "Calculate optimum attenuation" or run `perftest` |
| **Tuning rigour** (Estimate, Measure, Patient, Exhaustive, Take 1 minute; `wisdom.fftw`) (all) | FFTW planning effort | **Dropped from released plug-ins; optional in FFTW builds (PLUG-11):** released plug-ins use Ooura (Linux, Windows) or Accelerate (macOS), which have no planner to tune, because FFTW's planner is not thread-safe and another plug-in or the host may load the same FFTW ([plug-in builds](../../develop/fft-backends.md#plug-in-builds); [decisions](../decisions.md#releases)). A plug-in built with FFTW anyway (`-DCONVOLVER_FFT_BACKEND=FFTW`, which CMake warns about) plans with FFTW's quick estimate, and PLUG-11 offers measured planning there, with a tooltip stating its small measured gain ([FFT planning](#settings) below). The window always shows the FFT library and CPU kernels. The CLI keeps full FFTW tuning |
| **Dither** and **noise shaping** menus (property pages) | Applied only to 8- to 24-bit integer output | **Dropped from the plug-ins:** the old players wrote integer samples straight to the sound card, so Convolver was the last step and had to dither. A plug-in exchanges 32-bit floating-point audio with the host, and more processing (other plug-ins, faders) usually follows it; dither added there would be added again or undone later. Dither belongs once, at the final conversion to integer, which the host's render or the audio driver does (24-bit playback rounding is far below a converter's own noise). The 2006 VST2 window had no dither either. The CLI keeps both |
| Version and `http://convolver.sf.net` (property pages); help text with an e-mail address (VST2) | Identification and help | **Kept:** version in the title; links to the user guide and the GitHub issues instead of the retired address |
| OK, Cancel, Apply, Help (property pages) | Dialog buttons | **Dropped:** a plug-in window applies changes at once, as hosts expect, and the host's undo covers parameters |
| System-modal message boxes (all) | Every error and warning | **Replaced** by the status banner and the log |
| Preset menu, PRESET and RANDOM (above the VST2 window) | The host's own controls, not Convolver's | Host presets keep working; `convolver-preset` makes them |

Lessons from the 2006 VST2 implementation, which the threading rules below
avoid:

- `SelectFilter()` took `m_Convolution.lock`, the lock `processBase()` takes
  on the audio thread for every block, **before** opening the file selector,
  and held it while the user browsed and while the new convolution was built.
  Audio stopped (or the host's audio thread stalled) for as long as the dialog
  was open.
- Changing Partitions or Planning rigour rebuilt the whole convolution inside
  `setParameter()`, which hosts may call from the audio thread, under the same
  lock.
- Errors were `MessageBox(..., MB_SYSTEMMODAL)`, which blocks the host and can
  appear behind it.
- The bitmaps were loaded from paths on the author's own disk
  (`D:\Projects\convolver\VSTinterface\...`, in `convolverVST.rc`) and are not
  in the repository; their licence is unknown, so none are reused.

## Settings

Each setting is a host parameter in every format, so it works in generic
host controls, automation and host presets before and without the window.
The defaults leave today's sound unchanged: gain 0 dB in Set mode, mix 100%
wet, latency 256 frames. Every saved project and preset made before the
settings existed therefore keeps sounding the same.

**Gain** (PLUG-10, VST3-4, AU-7, LV2-7).

- Applied by `HostProcessor` to the filtered output, after convolution and
  before the mix, with the 64-sample ramp that bypass uses, so automation and
  typing a new value never click. Not applied to the dry path, which stays the
  untouched input.
- The Gain parameter always holds the user's Set value; the mode chooses
  which gain is applied. In Estimate and No clipping the window shows the
  computed value, read-only, beside the field, and the parameter keeps the
  Set value, so a computed gain outside the parameter's range is applied as
  it is and never clamped or written back to the host.
- Mode **Set** uses the typed value. **Estimate** and **No clipping** use
  `estimate_attenuation()`'s `noise_attenuation_db` and
  `worst_case_attenuation_db` for the filter being loaded, computed during
  preparation, off the audio thread, so the new filter arrives with its gain
  and the crossfade covers the change. The estimate convolves at least ten
  filter lengths of noise; PERF-3 measures what that adds to a load. When
  the estimate cannot be computed (`estimate_attenuation()` throws
  `std::overflow_error` for coefficients too large for single precision), a
  load in Estimate or No clipping mode is refused with that explanation, as
  the CLI's `--gain-db auto` stops; in Set mode the load goes ahead and the
  window shows the estimates as unavailable.
- Parameter: VST3 ID 0, the ID VST2's "Attenuation" had, rather than a new
  one ([decisions](../decisions.md#identities-and-file-formats)), but not the
  2006 mapping (0 to 1 linearly onto -200 to +200 dB, `float2dB` in
  `historical/convolverVST/convolverVST.cpp`), which is too coarse for a
  slider or automation. PLUG-10 proposes a range suited to room correction
  and crossovers (for example -100 to +24 dB, linear in decibels) for jrp to
  approve, because the mapping from the host's 0 to 1 value is saved in
  projects and automation and cannot change afterwards. The meaning is
  `--gain-db`'s, as the 2006 attenuation's was.

**Output peak and clip count** (PLUG-10). The audio thread keeps, per
instance, the largest output magnitude and the number of samples above full
scale since the last reset, as atomic counters written without locking or
allocating, like the fault counts. The control side reads them for the
window, the report and Copy diagnostics. They are meters, not parameters, and
are not saved.

**Mix** (PLUG-10). Wet/dry from 0 to 100%, applied after the gain
with the latency-aligned dry signal that bypass already computes, ramped like
bypass. With unequal input and output counts there is no dry signal (the
[bypass rule](../../develop/plugin-contract.md#bypass)), so the mix stays at
100% and the window says why. In VST3 it carries `kDryWetMix` through
`IParameterFunctionName`, which VST3-4 already proposed, so hosts can find it.

**Latency** (PLUG-10, VST3-4, AU-7, LV2-7).

- The block size B that `HostProcessor` already accepts, chosen from powers
  of two from 64 to 4096 frames (the current 256 stays the default). The
  window shows each choice as frames and milliseconds at the host's rate.
  Smaller B means less delay and more processor load, as fewer partitions
  meant in the old windows; larger B the opposite. Nonuniform partitions,
  which would give low latency and low load together, stay PERF-2.
- Changing it builds a new processor on the control side (as activation
  does) and tells the host: VST3 `restartComponent(kLatencyChanged)`, AU
  property-change notifications for `kAudioUnitProperty_Latency` and the
  tail, and LV2's latency port. Hosts may restart processing to compensate,
  so the change is not automatable and plays a short gap; the window says so.
- How the new value reaches the control side, per format, so that nothing is
  rebuilt on the audio thread and the host never reads a stale latency (each
  to be confirmed in its item):
  - VST3 (VST3-4): the controller owns the parameter. On a change it sends the
    new block size to the component in an `IMessage` (control thread), then
    calls `restartComponent(kLatencyChanged)`. The component records the
    value, reports it from `getLatencySamples()` from then on, and builds the
    new processor at the host's next `setActive(true)`, as activation does
    today. The processor ignores the parameter's queue in `process()`. A host
    that does not reactivate keeps the old size until it next activates;
    HOST-1 records which hosts do.
  - Audio Unit (AU-7): `SetParameter` may arrive on the render thread, so the
    adapter only stores the value in an atomic there. The rebuild happens on
    a control thread: at the next `Initialize`, or when the window or host
    sets it from the main thread, after which the adapter posts the latency
    and tail notifications. AU-7 chooses between the two and records it.
  - LV2 (LV2-7): `run()` compares the port with the active size and, when it
    differs, calls `schedule_work` once (allocation-free, as filter loads do).
    `work()` builds the processor, `work_response()` swaps it in, and the
    latency output port changes in the same `run()` as the swap.
- **Load**, beside it: the audio thread times each callback with the
  monotonic clock (no lock, no allocation) and keeps the 99th-percentile and
  largest share of the callback period in a small fixed histogram, which the
  control side reads. This replaces the old advice to time a calculation or
  run `perftest`, and is the evidence PERF-3 asks for, measured in the user's
  own host.

**FFT planning** (PLUG-11, optional; only in plug-ins built with FFTW, as jrp
asked).

- Released plug-ins use Ooura or Accelerate and show no planning control. A
  plug-in built with `-DCONVOLVER_FFT_BACKEND=FFTW` shows a Planning choice
  beside Latency: Quick (FFTW's estimate; the default and today's behaviour)
  or Measured (FFTW's measure). Patient is not offered: on x86-64 it matched
  measured within noise and took 2–5 times longer to plan.
- Its tooltip, and a line in Details, state what was measured, so nobody
  expects much. jrp asked for "at most 6%" if that is what was found; the
  whole-engine tests support only "about 2–6% faster" for the filters they
  covered, so the draft reads: "Measured planning made Convolver about 2–6%
  faster in our tests; long filters gain least, because the FFTs are a small
  part of their work. Each load takes longer (4–26 ms on Apple Silicon, up to
  0.5 s per block size on x86-64), and FFTW's planner is shared with any
  other plug-in in the host that uses FFTW." The figures are from the
  [measurements](../../develop/fft-backends.md#fftw-implementation-and-planning-comparisons)
  and [where the time goes](../../develop/fft-backends.md#where-the-time-goes):
  whole-engine gains of about 2–6% on some of eight workloads on Apple
  Silicon, and on x86-64 FFTs 1.2–1.8 times faster alone but under 4% of the
  work for filters of 16,384 taps or more. The same breakdown puts the FFTs at
  about 50% of the work for a 256-tap filter at B=64 and 20% for 4096 taps at
  B=256, which implies gains of very roughly 8–22% for short filters at small
  blocks, a case the whole-engine tests did not cover. So PLUG-11 first
  measures short filters at small blocks on x86-64 and the current engine,
  then writes the tooltip's figure (an "at most" only if the measurements
  support one) with the date in its source comment.
- Planning runs while the processor is prepared on the control side, never on
  the audio thread. Convolver already serialises its own planner calls; other
  FFTW users in the host are not covered, which is why CMake warns about FFTW
  plug-in builds and why this stays a build-specific option.
- Saved with the other settings, written only when Measured is chosen (VST3
  host state version 3); not automatable, and a change prepares a new
  processor as a Latency change does. How the Audio Unit and LV2 save it
  follows the saved-form choice below.
- Loading a wisdom file made by `convolver --tune-fft` would remove the
  planning delay; it is left out unless asked for.

**Where settings are saved.** Each format saves the settings in its own
usual way, and the differences are documented rather than smoothed over
(jrp: "live with the platform defaults for now, but document the potential
source of divergence"; [decisions](../decisions.md#identities-and-file-formats)).

- VST3 saves its parameters in the component state, as it does Bypass. The
  host state codec gains the settings in a new version (3), written only
  when a setting differs from its default, so every state that does not use
  them is still written as version 1 and older builds keep reading it, as the
  [host state rules](../../develop/plugin-contract.md#host-state) require.
  Version 3 is a superset of version 2: it also carries the failed-first-load
  mark, so a failed load with non-default settings keeps both. PLUG-10 adds 3
  to `host_state.versions` in `plugins/identities.json`.
- Audio Units save parameters themselves in `ClassInfo`; the adapter
  ignores the settings' values in a state from another format, as it does
  for bypass.
- LV2 adds control input ports, which hosts save. Adding ports changes the
  plug-in's interface; the old ports keep their indices and symbols, and
  hosts restore older sessions with the new ports at their defaults. To check
  with Ardour, Carla and PipeWire `filter-chain` in LV2-7.
- `convolver-preset` gains no setting options for now: its presets carry the
  filter only. If users want presets to carry settings, it could later take
  the CLI's `--gain-db` and `--block-size` and a new `--mix`.

**Where the settings can diverge**, documented in
[how the plug-ins behave](../../use/plugins.md) and in TODO's limitations
when PLUG-10 lands, each with what the user sees:

- A preset made by `convolver-preset` sets the filter only. In VST3 the
  settings return to their defaults, because a version 1 state has none; in
  the Audio Unit and LV2 they keep whatever values the host has, unless the
  host also restores its own parameter values (each case checked in VST3-4,
  AU-7 and LV2-7).
- A state moved between formats (for example an AU preset's
  `convolver-state` read by VST3) carries the filter but not the settings.
- The same project can therefore play at a different gain, mix or latency in
  another format or host. The window's Details line and the log show the
  values in use, so a report shows the difference.
- Hosts that restore an older LV2 session or Audio Unit preset give new
  settings their defaults, which keep today's sound.

**Filter conveniences** (VST3-11, AU-6; the shared parts in PLUG-9).

- Drag and drop of a filter file onto the window.
- Recent files, kept per user in a small text file beside the Convolver log,
  not in the project, so they are shared by every host and instance. A new
  file Convolver writes (agreed by jrp, with drag and drop and reload on
  change).
- Reload when the file changes, while the window is open: the window checks
  the file's modification time once a second on its own timer and asks for a
  reload, which crossfades as any replacement does. For a `.cfg`, the files
  it names are checked too.
- For a list, the entries with the one chosen and why the others were not,
  from the selection steps that `inspect_configuration()` already records.

**A further idea, not proposed for now:** an A/B comparison that holds two
filters and switches between them with the existing crossfade, for comparing
room corrections. It needs two prepared filters in memory and a rule for
which one is saved, so it waits for a request.

## Framework choice

jrp asked for stability and longevity over features
([direction](../decisions.md#direction)), and the
[plug-in formats plan](2026-10-21-plugin-formats.md#development-frameworks)
chose each format's own headers over a framework, naming DPF as the fallback
"if a shared editor across formats is ever needed". The window is small
(about twenty standard controls), so the question is which costs less to
keep working for twenty years: three thin native views, or one toolkit.

| Option | Licence | Formats and systems | Accessibility | Longevity and fit | Verdict |
| --- | --- | --- | --- | --- | --- |
| **Native views** behind the format's own interface: `IPlugView` (VST3), an `AUCocoaUIBase` view factory (AU), the LV2 UI extension | Ours (GPL-2.0-or-later); no new dependency | AppKit `NSView` on macOS, shared by VST3 (`kPlatformTypeNSView`), the AU's Cocoa view and an LV2 `ui:CocoaUI`; Win32 `HWND` for VST3 and LV2 `ui:WindowsUI`; Linux X11 needs its own decision (below) | Standard AppKit and Win32 controls are exposed to VoiceOver and to UI Automation (NVDA, JAWS, Narrator) by the systems themselves | AppKit and Win32 are the oldest stable GUI interfaces on their systems; the code depends on nothing that can be abandoned. Costs one view per system (estimate 400 to 700 lines each), but the content is shared code | **Chosen** |
| VSTGUI 4.15 (Steinberg) | BSD-3-Clause ([Debian copyright file](https://metadata.ftp-master.debian.org/changelogs/main/v/vst3sdk/stable_copyright)) | Windows, macOS, Linux X11 (preliminary Wayland in 4.15, [3.8.0 notes](https://forums.steinberg.net/t/vst-3-8-0-sdk-released/1011988)); a VST3 editor class and WYSIWYG UI descriptions. Usable inside an AU Cocoa view or an LV2 UI only with glue of our own | None: "there is currently no support for accessibility in VSTGUI" (its maintainer, [November 2021](https://forums.steinberg.net/t/accessibility-with-vstgui/751087)); custom-drawn controls are invisible to screen readers | Maintained by Steinberg since 2003, released with the VST3 SDK we already build with, but a fifth SDK submodule (`vstgui4`) and a shipped dependency (with Linux libraries such as cairo, xcb and fontconfig, to be confirmed). Follows Steinberg's priorities | **Linux VST3 stop-gap** (VST3-13; jrp, 25 October) |
| DPF (DISTRHO Plugin Framework) | ISC; its own VST3 and AU code, which does not use Steinberg's SDK ([licensing](https://github.com/DISTRHO/DPF/blob/main/LICENSING.md)) | LV2, VST2, VST3, CLAP, AU, standalone; one UI class for all, drawn by its DGL widget library through pugl | None for its drawn widgets | Active, largely one maintainer. Its UI is tied to its plug-in wrapper: using DPF's editor means replacing our VST3, AU and LV2 adapters with DPF's (or maintaining a fork), redoing the contract, PLUG-7 lifecycle tests, Status parameter, LV2 worker and state handling, and first proving it keeps our permanent class IDs, AU codes, LV2 URIs and state formats | Not recommended: the shared part the fallback was meant for can be our own code |
| pugl (window embedding) plus a drawing library | ISC, plus the drawing library's | All three formats and systems from one code base | None | Small and stable, but we would write every widget and still a file dialog per system | Not recommended |
| A web view (WKWebView, WebView2, WebKitGTK) | Varies | Possible everywhere | Good | WebView2 is a separate runtime on Windows; WebKitGTK inside a host process is a large dependency that conflicts with hosts' own toolkits; a browser engine for a status window | Not recommended |
| JUCE 8 | AGPLv3 or commercial | All | Good | Excluded by the formats plan for its licence and vendor control | Excluded |

**Linux.** No native toolkit embeds well in another program's window: a host
may itself use GTK 2 (Ardour), GTK 3 or Qt, and loading a second version of
a toolkit into it is a known cause of crashes, which is why LV2 discourages
toolkits in plug-in UIs. The realistic choices for VST3-13 are a small X11
view drawn with xcb (and cairo for text), or VSTGUI for Linux only, or no
Linux window: Linux users already have LV2, whose generic host UIs show a file
chooser for the `filter` parameter and the status port, and the VST3 Status
parameter. Neither drawn option is accessible. jrp chose VSTGUI for the
Linux VST3 window only, as a stop-gap ([decisions](../decisions.md#direction)): it saves writing a third
view where no accessible native option exists anyway, and the first preview
needs a window on Linux too. A native X11 view can replace it later if Linux
users need one. VSTGUI is then a shipped dependency of the Linux VST3 build
only (BSD-3-Clause, compatible with our GPL), so VST3-13 adds it and the
libraries it links to the package checks and the licence notices. Using VSTGUI for VST3 on every system and a
native view for the Audio Unit was considered and set aside: it means two
different windows on the Mac, and VST3, the most used format, would lose
screen readers.

## How the window connects

The window is a client of the control side and reuses what DIAG-1 and PLUG-8
built. Nothing in it is a second source of truth.

**A shared report (PLUG-9).** A new value type in `Convolver::host` (name to
be settled; `HostReport` here), built under the adapter's control lock from
`HostFilter`, the known `HostLayout`, the processor's latency, tail and fault
counts, the execution facts and the log state. It holds the status, the status
line, the full failure, notice and fault messages with request numbers, the
accepted request (source and path), host and filter details, the log path and
whether it is written, and the settings with their computed gains and the
meters. Two shared functions go with it:

- `report_fields()`: the report as a flat list of named values, which each
  format sends in its own way (VST3 message attributes, an AU property
  dictionary, LV2 atoms). The names are internal between our view and our
  plug-in, which always ship together, so they are not permanent identities.
- `report_text()`: the multi-line text that Copy diagnostics produces and
  that the window's status area shows, worded like the log.

**Loading a file writes preset state.** Choosing a file does exactly what
`convolver-preset` does
([preset files](../../develop/plugin-contract.md#preset-files)): it calls
`load_configuration` with the host's sample rate and channel counts (so a list
picks the same entry as the CLI and the preset tool), and makes a `HostState`
with the filter's coefficients embedded. That state goes through the same
`HostFilter::load` as a host's state restore, with
`HostRequest{HostFilterSource::file, path}`, so the contract's rules for
failed loads, replacement, crossfading and saving apply unchanged, and the
failure reason names the file. Afterwards the plug-in's saved state is
byte-for-byte what `convolver-preset` writes for the same rate and channels,
which a test checks. PLUG-9 moves the preset tool's file-to-state step into a
shared function so both use one code path.

- **Before the host is configured** (VST3 before activation, AU before
  initialization), lists still need a rate to choose an entry. The load uses
  the rate and channels the host has proposed so far, says so in the status
  ("checked when the host starts it"), and the usual check at activation
  applies.
- **The chosen path is not saved** in the plug-in state, which stays
  independent of where filter files were, as presets are. The window shows
  the path for loads in the current session and the log records it; after a
  project is reopened it says the filter came from the saved project. Keeping
  the path for display across sessions would be a saved-format change
  (VST3's controller state, which is empty today, or an extra AU `ClassInfo`
  key): see [decisions](#decisions-for-jrp).
- **Settings are parameters.** The window changes gain, mode, mix and
  latency as any generic host control would (VST3 `beginEdit`,
  `performEdit`, `endEdit`; AU `AUParameterSet` with gesture events; LV2
  port writes), so host automation, undo and presets see every change, and
  the processor learns of it the way it already learns of Bypass.
- **The host is told the project changed**: VST3 `IComponentHandler2::setDirty`
  (and the existing `restartComponent(kParamValuesChanged)` for the Status
  text); AU a property-change notification for `kAudioUnitProperty_ClassInfo`;
  LV2 the `filter` parameter's `patch:Set`, as today.

## Threads

The audio thread plays what `HostProcessor` was handed and signals faults as
it does now. Its only new work is applying the gain and mix with ramps and
updating the meters, all without locking, allocating or doing I/O, as the
[real-time rule](../../develop/architecture.md#invariants-to-preserve)
requires. The rules for the window:

1. **The window runs on the host's UI thread and talks to the controller
   side only.** In VST3 the view holds the `Controller` (or a narrow interface
   on it), never the `Processor`; in AU it holds the `AudioUnit` handle and
   uses only `AudioUnitGetProperty`, `AudioUnitSetProperty` and Audio Unit
   events, as Apple asks, "by value, never by reference"
   ([Audio Unit view guide](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/TheAudioUnitView/TheAudioUnitView.html)),
   so it keeps working when a host runs the unit in another process; in LV2
   it uses only the UI extension's port writes and port events, never
   `instance-access`.
2. **The control side pushes reports; the window copies them.** The processor
   (VST3) or unit (AU) builds a report after every load, activation,
   configuration change and fault explanation, at the points where it already
   sends the Status line, outside its control lock. The controller keeps the
   latest copy under its own small lock; the window refreshes from that copy
   on its own thread, from a timer while it is open (a few times a second) or
   a posted update, and never calls back into the processor to read.
3. **Loads are requests, not actions in the window.** The window asks
   (`ConvolverLoad` message with the path, an AU custom property, an LV2
   `patch:Set`); the control side does the reading and preparing, under the
   same lock and on the same kind of thread as a host's state restore. VST3-5
   (slow preparation on the host's thread) applies to it unchanged.
4. **No lock is held while a dialog is open,** and no dialog is modal for the
   host where the system allows otherwise (`NSOpenPanel` as a sheet or with a
   completion handler; Windows' `IFileOpenDialog` is modal to its owner
   window, as every Windows file dialog is). The 2006 window's stall cannot recur, because the window
   never holds the control lock at all.
5. **Clipboard, log reading and "show file" run on the UI thread** and read at
   most the last 64 KiB of the log; they never write it.
6. **Gain estimates run with filter preparation**, off the audio thread; a
   latency change builds the new processor on the control side, as
   activation does. Neither runs in a parameter callback that a host may call
   from the audio thread (the 2006 plug-in rebuilt its convolution there).
7. **Meters are read, never written, by the control side:** the audio thread
   updates the output peak, clip count and callback timings as relaxed atomic
   values, and a reset from the window is a request the audio thread acts on
   at its next callback.

## Per format

**VST3 (VST3-3, VST3-11 to VST3-13).**

- `Controller::createView("editor")` returns an `IPlugView` (the SDK's
  `CPluginView` helper, which needs no VSTGUI) for `kPlatformTypeNSView`,
  `kPlatformTypeHWND` and, in VST3-13, `kPlatformTypeX11EmbedWindowID`
  (whose timers and file descriptors go through the host's `IRunLoop`);
  `kPlatformTypeWaylandSurfaceID` (preview in SDK 3.8) only once hosts other
  than Studio One use it.
- Content scaling with `IPlugViewContentScaleSupport` on Windows (macOS
  scales natively); resizable with a minimum size, so long messages wrap
  rather than clip.
- Messages: `ConvolverStatus` gains the report's fields (or a new
  `ConvolverReport` message carries them); a new `ConvolverLoad` message
  carries a UTF-8 path from controller to processor. The processor's
  `notify()` loads it as `setState` does. Bypass (parameter 1000) and the
  settings are edited through the controller (`beginEdit`, `performEdit`,
  `endEdit`).
- Parameters (VST3-4): Gain (ID 0, with the range PLUG-10 proposes), Gain
  mode (a list: Set, Estimate, No clipping), Mix (`kDryWetMix`)
  and Latency (a list, not automatable), with IDs from 1002 recorded in
  `plugins/identities.json` and frozen by `tests/identities_tests.py` like
  1000 and 1001. Gain's ID 0 is recorded the same way, and
  [plugin identities](../../develop/plugin-identities.md), which today keeps
  IDs 0 to 2 free, says ID 0 is now Gain with a new mapping and 1 and 2 stay
  free.
- `editorhost` stops being useless: it currently exits with "EditController
  does not provide its own editor"
  ([host testing](../../develop/host-testing.md#steinbergs-sdk-hosts)).

**Audio Unit (AU-6).**

- `kAudioUnitProperty_CocoaUI` names a view factory class in the component
  bundle; its `uiViewForAudioUnit:withSize:` returns the same AppKit view as
  VST3-11, driven through an AU-specific connection object.
- Objective-C class names share one namespace per process, so two Convolver
  builds loaded in one host (an old and a new version) would clash and the
  host would get either one's view (the same problem hit Steinberg's AU
  wrapper, [forum](https://forums.steinberg.net/t/name-clashes-with-auwrapper/201670)).
  The factory and view classes therefore carry the version in their names,
  generated from `VERSION` at build time, and nothing persistent refers to them.
- Custom properties (IDs from 64000, Apple's range for private properties),
  with Core Foundation values ([QA1684](https://developer.apple.com/library/archive/qa/qa1684/_index.html)):
  a read-only report (a `CFDictionary` of the report fields) with a
  property-change event whenever it changes, and a writable load request (a
  `CFString` path). The view listens with an `AUEventListener` on the main
  run loop for the report, the Status parameter (1001) and bypass.
- Parameters (AU-7): the same settings as global Audio Unit parameters, with
  IDs recorded beside Status (1001) in `plugins/identities.json`; Latency is
  indexed and not automatable.
- **Sandboxed hosts** (GarageBand): the system file dialog grants the host
  access to the chosen file only. A `.cfg` names other files, which the
  sandbox may refuse, so the message for an unreadable filter file inside a
  sandboxed host must say so and suggest a preset made with
  `convolver-preset` (which embeds the filters) or choosing the folder. To be
  checked in GarageBand during AU-6.
- The Audio Unit's licence (GPL-3.0-or-later with the AudioUnitSDK's notices)
  covers the shared view code, which stays GPL-2.0-or-later.

**LV2 (LV2-7 for the settings; LV2-6 for the window, optional).**

- Settings (LV2-7): control input ports for gain, gain mode, mix and latency,
  with port symbols jrp names, after the existing ports; changing latency
  updates the latency output port.

- Generic host UIs already give LV2 users a file chooser (the `filter`
  parameter) and the status port, so LV2 needs the window least.
- The DSP side would send the report as atoms on its notify port: `patch:Set`
  messages for new read-only properties (the full message, filter details),
  written by `run()` from text prepared in `work()` and handed over through
  `work_response()`, in buffers sized at instantiation, so `run()` still never
  allocates. New property URIs are additions jrp names (the
  [identities rule](../../develop/workflow.md#while-working)), and the notify
  port's buffer size (`rsz:minimumSize`) must hold the longest message.
- The UI is a separate binary declared in the Turtle files (`ui:CocoaUI`,
  `ui:WindowsUI`, `ui:X11UI`), reusing the macOS and Windows views, and
  passes `lv2lint`'s UI checks.

## Accessibility

Goals for every window phase. jrp counts accessibility as highly desirable but
not essential, so a phase that misses one records the gap in its
pull request and in TODO instead of waiting for it. Native controls meet
most of them without extra work:

- Every control has an accessible name and role; the status and the full
  message are readable text that a screen reader reads in order and the user
  can select and copy (a read-only text view, not a drawn label).
- The window works from the keyboard alone: Tab order top to bottom, Space or
  Return on buttons, the file dialog reachable without a mouse. Hosts route
  keys differently (REAPER has "send all keyboard input to plug-in"; VST3
  hosts may use `IPlugView::onKeyDown`), so the host smoke tests check it.
- Every slider has a typed number field beside it, the gain modes are radio
  buttons, and the meters and load are text, not only bars, so each value is
  read and set exactly from the keyboard and by screen readers.
- Status is never shown by colour alone: the status word is always written,
  and colours follow the system's appearance (dark mode, high contrast,
  increased contrast) because the controls are native.
- Text follows the system's text size where the platform has one, and the
  window resizes to fit rather than clipping.
- Checked with VoiceOver on macOS and NVDA or Narrator on Windows in a named
  host (REAPER, with the OSARA extension, is widely used by blind musicians). The
  CI tests below check names and roles automatically.
- Linux drawn views cannot be reached by screen readers (AT-SPI comes from
  toolkits), so the VSTGUI stop-gap is not accessible; the documentation
  says so and points to LV2's generic UI, the Status parameter and the log.

## Testing

Most of the window's behaviour is shared code and is tested without a
display, in the existing CTest targets and CI jobs:

| What | How | Where |
| --- | --- | --- |
| Gain, mix and latency | The double-precision oracle tests in `tests/native/engine_tests.cpp` and the host comparisons run with gains, mixes and each block size, reporting the worst error relative to the output peak; ramps checked sample by sample; defaults bit-identical to today's output | `native.engine`, `native.host`, the PLUG-2 harness |
| Saved settings | State version 3 round trip; states with default settings still written as version 1, byte-identical to today's; `convolver-preset` presets and states moved between formats give the documented settings in each format; AU and LV2 restore older sessions with defaults | `native.state`, `plugins.presets`, each format's adapter tests |
| Latency changes | Changing latency while active: the reported latency and tail follow, the format's change notification is sent, no audio-thread allocation (the audio guard), and the PLUG-7 sequences with a latency change between steps | `vst3.lifecycle`, `au.lifecycle`, `lv2.lifecycle` |
| Meters | Peak, clip count and timings from known signals; allocation-free under the audio guard; reset requests | `native.host` |
| Report content and text | Table-driven: one case per `HostFailureKind`, notice, fault kind, Not checked yet, and log not writable, each checking `report_fields()` and `report_text()`; this can share DIAG-1's [diagnostics by surface](2026-10-24-diagnostics-by-surface.md) table | `native.host` |
| Load from a file equals the preset tool | For a WAV, a `.cfg` and a list, the state after a load from the window's path equals `convolver-preset`'s output byte for byte, and a refused file keeps the previous filter (PLUG-2 comparison of the audio) | `plugins.presets`, the PLUG-2 harness |
| VST3 messaging | Through the existing controller and processor in the test harness: `ConvolverLoad` loads, refuses with the right reason, calls `setDirty`, and the controller's report copy follows every load, activation and fault | `vst3.adapter`, `vst3.lifecycle` |
| AU properties | Report and load properties through `AudioUnitGetProperty`/`SetProperty`, with property-change events, under the same failure cases | `au.adapter` |
| LV2 atoms | The lilv test host reads the report atoms after loads and refusals; `run()` stays allocation-free under the audio guard | `lv2.adapter` |
| View opens and closes | Steinberg's `editorhost` (Linux under `xvfb-run`; built from the SDK in use with its hosting examples, which on Linux need GTK 3 and gtkmm 3 in CI only) and pluginval without `--skip-gui-tests`, which opens the editor, also while processing | `vst3.pluginval`, a new `vst3.editorhost` |
| AU view | `auval` lists the Cocoa view and checks it loads ("Cocoa Views Available") | The `auval -strict` step of the AU workflow |
| Accessibility and content | A small test host per platform attaches the view to an off-screen window, walks the accessibility tree (`accessibilityChildren`, labels and roles on macOS; Win32 window text and `IAccessible` on Windows), checks the status text equals `report_text()`, presses Copy diagnostics and reads the clipboard, and saves a screenshot as a CI artifact for review (not compared pixel by pixel) | `vst3.editor`, `au.editor` |
| File dialog | Not automated: tests inject the chosen path through a test hook; the real dialog is a step in the host smoke tests | [host testing](../../develop/host-testing.md) |

The GitHub-hosted macOS runners have a logged-in desktop session, so views
open there; whether the Windows runners allow windows to be created and
measured is checked in VST3-12's first CI run. The host smoke tests in
[host testing](../../develop/host-testing.md#smoke-test-in-each-first-host)
gain steps for the window in REAPER, Audacity (including its separate
real-time copy) and GarageBand: open it, load a wrong-rate filter and read
the message, reload, change gain mode and latency and confirm the host
compensates, copy diagnostics, show the log, and read it with VoiceOver.

## Phases and TODO items

Each phase is its own pull request, follows the
[workflow](../../develop/workflow.md), and is useful without the next.

| Phase | Item | Delivers | Depends on |
| --- | --- | --- | --- |
| 1. Shared, no window | **PLUG-9** | `HostReport`, `report_fields()`, `report_text()`, and the preset tool's file-to-state step as a shared function; headless tests | Nothing |
| 1 | **PLUG-10** | Gain (three modes), mix and latency in `HostProcessor` with ramps; meters; host state version 3; the documented divergence between formats; oracle and state tests; a contract change in its own pull request first | jrp's choices on settings |
| 1 (optional) | **PLUG-11** | Quick or Measured FFT planning in FFTW-built plug-ins, with the tooltip stating the measured gain; planning options passed through `HostProcessor` | PLUG-10 |
| 1 | **VST3-3** | Controller report copy, `ConvolverLoad`, `setDirty`, Bypass edits from the controller; headless tests. No `createView` yet | PLUG-9 |
| 1 | **VST3-4** | The settings as VST3 parameters, saved in the component state, with latency changes reported to the host | PLUG-10 |
| 1 | **AU-7** | The settings as Audio Unit parameters, with latency and tail notifications | PLUG-10 |
| 1 | **AU-6** (first half) | Report and load properties with change events; headless tests | PLUG-9 |
| 2. macOS window | **VST3-11** | The AppKit view and its VST3 `IPlugView`, with drag and drop, recent files and reload on change; `editorhost`, pluginval GUI tests and the accessibility test on macOS CI; smoke steps in REAPER and Audacity; user guide section | VST3-3, VST3-4 |
| 2 | **AU-6** (second half) | The Cocoa view factory over the same view, versioned class names; `auval` view check; GarageBand smoke steps, sandbox check | AU-6 first half, AU-7, VST3-11 |
| 3. Windows window | **VST3-12** | The Win32 view, content scaling, keyboard navigation, Windows CI view test | VST3-11 |
| 4. Linux stop-gap | **VST3-13** | The window in VSTGUI for Linux VST3 only, driven by the host's `IRunLoop`; VSTGUI and its libraries in the package checks and licence notices; `editorhost` under `xvfb-run` | VST3-3, VST3-4; can run alongside VST3-11 and VST3-12 |
| 1 | **LV2-7** | The settings as LV2 control ports, with older sessions checked in named hosts | PLUG-10 |
| 5. LV2 (optional) | **LV2-6** | Report atoms (new URIs named by jrp), then an LV2 UI reusing the views | VST3-11/12, a need shown by Linux users |

Documentation as the phases land: the window in
[how the plug-ins behave](../../use/plugins.md) (which today says Convolver
"has no window of its own") and [troubleshooting](../../use/troubleshooting.md);
the messages and the report in the
[adapter contract](../../develop/plugin-contract.md#status-and-log) (a contract
change, in its own pull request, before VST3-3); the new folder for view code
(proposed `plugins/editor/`, with `mac/` and `windows/`) in the
[implementation map](../../develop/architecture.md); tests in
[testing](../../develop/testing.md).

**The first preview waits for the settings and the windows**
([decisions](../decisions.md#releases)): PLUG-9, PLUG-10, VST3-3, VST3-4 and VST3-11 to VST3-13 for VST3 on
macOS, Windows and Linux, and AU-6 and AU-7 for the Audio Unit, which jrp
moved into the first preview (with its own AU-3 to AU-5). The
REL-2 journeys then load a filter from the window as well as from a preset,
and the host smoke tests (HOST-1, and AU-4 in GarageBand) gain the window's
steps. LV2-7 joins the preview that adds LV2; PLUG-11 and LV2-6 are not
needed for any preview. The windows can be built in parallel once VST3-3
and VST3-4 are in, as they share the report code and differ only in views.

## Risks and open questions

- **Hosts that open a different instance in the window.** Audacity's
  real-time window shows a copy that is not the playing one
  ([status and log](../../use/plugins.md#status-and-the-convolver-log)).
  The window must say which instance it is and when that instance has not been
  started, or users will debug the wrong copy. Loading a filter in that window
  loads it in that copy only; whether Audacity then passes the state to the
  playing copy is checked in HOST-1.
- **Slow loads on the UI thread.** A long multichannel filter is read and
  prepared on the host's thread, as a state restore is today, so the window
  cannot repaint meanwhile. Since jrp's review of `6920a45` (31 October 2026)
  this is an acceptance gate for every window, not only a risk: measured on
  the PERF-3 supported workloads by automated tests rather than by hand in a
  host, a load must not pause the host noticeably,
  or preparation moves to the background first (VST3-5 for VST3, the same in
  the Audio Unit), with the window showing "Loading". LV2's request numbers
  and prepare-then-publish are the pattern, and the adapter owns requests,
  overtaken requests and shutdown whether or not the window is open
  ([loading responsiveness](../../../TODO.md#loading-responsiveness)).
- **Report size.** A list that matches nothing explains every candidate, which
  can be long. VST3 message attributes and AU dictionaries carry it easily;
  LV2's notify port needs a size declared up front (LV2-6).
- **Out-of-process Audio Units.** Hosts that run a version 2 unit in another
  process (for example under Rosetta) pass properties between processes;
  custom properties with Core Foundation values should survive that, which
  AU-6 checks in a host that does it.
- **Latency changes in hosts.** Hosts differ in how they react to a latency
  change (some restart processing, some recompensate only at the next
  playback start, some ignore it until the project is reopened). HOST-1
  records what each named host does, and the window's note says what to
  expect.
- **The Estimate takes time.** Ten filter lengths of noise for a long
  6-channel filter can add a noticeable part of a second to each load; PERF-3
  measures it. If it is too slow, it can run after the load on a worker,
  with the gain ramped to the new value when it is ready.
- **Gain and old projects.** Gain shares VST2's parameter ID 0 but not its
  mapping, so if VST3-6 ever declares the VST2 plug-in compatible, hosts that
  map VST2 parameter *n* to VST3 ID *n* would apply an old normalized
  attenuation to the new range and get a different gain. VST3-6 must then
  convert the value itself (in its state converter) or leave parameter
  values out of the migration.
- **The preview's schedule.** The settings, three VST3 windows and the Audio
  Unit (its view, parameters, validation and packaging) are now on the path
  to the first preview, and they are the largest part of it. PLUG-9 and
  PLUG-10 can start at once, alongside PLUG-5 and REL-2; the VST3 windows
  need VST3-3 and VST3-4 first, and the Audio Unit's view needs AU-7 too. GarageBand offers only mono and stereo
  effects at 44.1 kHz, so the Audio Unit's 2→6 journey needs a second host.
- **Key handling and focus** differ by host; the smoke tests check that typing
  in the window does not trigger the host's shortcuts and vice versa.

## Decisions for jrp

All answered (recorded in the [decisions log](../decisions.md)). Still to come, as each phase proposes
them: the Gain range (PLUG-10), new parameter IDs and LV2 port symbols.

1. **Framework (decided):** native views (AppKit, Win32), no framework, with
   the content in shared code, rather than VSTGUI for every system (one code
   base, no accessibility, a new shipped dependency) or DPF (replaces our
   adapters). This replaces the formats plan's "DPF is the fallback for a
   shared editor": the shared part is our own report code
   ([decisions](../decisions.md#direction)). Linux uses VSTGUI as a
   stop-gap (see 6).
2. **Settings:** Gain with the Set, Estimate and No clipping modes, Latency,
   Mix (the old "Effect level"; decided) and meters, as host parameters in
   every format, defaults leaving today's sound unchanged; dither is not
   brought back, and tuning rigour returns only as PLUG-11's optional
   planning choice in FFTW-built plug-ins, as jrp asked.
3. **Gain parameter (decided):** VST3 ID 0, with no new ID, but a new
   mapping rather than the 2006 one ([decisions](../decisions.md#identities-and-file-formats)).
   Its range is jrp's to approve when PLUG-10 proposes it.
4. **Saved form (decided):** each format's own way for now: VST3 host
   state version 3, written only when a setting is not at its default;
   Audio Unit and LV2 settings saved by the host as parameters and ports;
   no setting options in `convolver-preset` yet. The places where settings
   can diverge between formats and hosts are documented
   ([where the settings can diverge](#settings)). New parameter IDs and LV2
   port symbols are jrp's to approve when PLUG-10 proposes them.
5. **Conveniences (decided):** drag and drop, recent files (a small per-user
   file beside the log) and reload when the file changes, all three.
6. **Order (decided):** settings first (PLUG-9, PLUG-10, then each format),
   then the macOS window shared by VST3 and the Audio Unit, then Windows;
   the Linux VST3 window in VSTGUI as a stop-gap; LV2's UI only if Linux
   users ask.
7. **The chosen path (decided):** shown for the current session and written
   to the log, not saved with the project. Saving it for display would add
   VST3 controller state and an AU `ClassInfo` key, a format change.
8. **Accessibility (decided):** highly desirable, not essential. The goals
   above are checked in each window phase (VoiceOver in REAPER), and gaps are
   recorded rather than blocking.
9. **Previews (decided):** the first preview waits for the settings and the
   three VST3 windows, and holds the Audio Unit with its settings and the
   shared macOS window.
