<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in editor plan: a window for loading and debugging filters

Status: written on 24 October 2026 against Convolver `80ac07a` (0.4.45), at
jrp's request ("I think that doing that would be helpful, not least for
debugging"). Nothing here is implemented, and the framework, scope and order
wait for jrp's choices in [decisions for jrp](#decisions-for-jrp). Open work is
tracked as PLUG-9, VST3-3, VST3-11 to VST3-13, AU-6 and LV2-6 in
[TODO](../../../TODO.md#plug-ins-vst3-lv2-and-au); HOST-2 is the umbrella item.
The rules every adapter follows stay in the
[plug-in adapter contract](../../develop/plugin-contract.md); where this plan
and the contract seem to disagree, the contract wins until a change to it is
decided and merged in its own pull request.

For jrp, deciding whether and how to build the editor, and for whoever then
implements one of its phases.

**In short:**

- **What the window is for.** One small window, the same in every format,
  that shows what the instance is doing and why: the Status value and the
  full explanation of any failure or fault (what failed, why, what to
  change), what is loaded and how the host started it (rates, channels,
  paths, filter length, latency, tail, FFT library and CPU kernels), and
  where the Convolver log is, with buttons to copy a diagnostic report and to
  show the log file. It also lets the user choose a filter (`.cfg`, list or
  sound file), which loads exactly the state `convolver-preset` would write
  for the host's rate and channels, and switch Bypass. It adds no
  sound-changing controls.
- **Framework: native views, no framework** (recommended). One AppKit view
  on macOS serves VST3 and the Audio Unit (and an LV2 Cocoa UI later); a
  Win32 view serves VST3 on Windows; Linux comes last, with its toolkit
  decided then. Native controls give screen readers, keyboard use, dark mode
  and scaling without work of our own, and AppKit and Win32 have outlived
  every plug-in GUI framework. VSTGUI (BSD-3-Clause, Steinberg) and DPF
  (ISC) were weighed: VSTGUI has no accessibility support and adds a shipped
  dependency, and DPF's editor cannot be used without replacing our three
  adapters with DPF's own. JUCE stays excluded for its licence.
- **Threads.** The window talks only to the controller side (the VST3 edit
  controller, AU properties, LV2 UI messages), never to the processor or the
  audio thread. It reads a report the control side builds, and asks for a
  file load the same way a host restores state; the audio thread only ever
  plays what it was handed, as today.
- **Shared first, windows second.** The first phase (PLUG-9, VST3-3, then
  AU-6's properties) needs no window at all: a shared report and a
  load-from-file path, tested headlessly through the existing harnesses. The
  macOS window (VST3-11, reused by the Audio Unit) follows, because jrp's test
  hosts (REAPER, Audacity, GarageBand) are on his Mac; Windows (VST3-12),
  Linux (VST3-13) and an LV2 UI (LV2-6) after that.
- **Testing.** Everything the window shows is built and formatted by shared
  code that the existing headless tests check per failure kind. The windows
  themselves are opened in CI by Steinberg's `editorhost` (under `xvfb-run`
  on Linux), by pluginval's GUI tests (dropping `--skip-gui-tests`), by
  `auval`'s custom-view check, and by a small test per platform that walks
  the window's accessibility tree.

Contents: [what the window does](#what-the-window-does) ·
[the 2006 VST2 window](#the-2006-vst2-window) ·
[framework choice](#framework-choice) ·
[how the window connects](#how-the-window-connects) ·
[threads](#threads) · [per format](#per-format) ·
[accessibility](#accessibility) · [testing](#testing) ·
[phases and TODO items](#phases-and-todo-items) ·
[risks](#risks-and-open-questions) · [decisions for jrp](#decisions-for-jrp)

## What the window does

The window exists first to explain, and second to load. jrp wants
diagnostics that explain generously
([decisions](../decisions.md#diagnostics)), and today the full explanation
of a failure is only in the Convolver log, a file most users never open, while
the host shows at most the Status parameter's one line of 127 characters
([status and log](../../develop/plugin-contract.md#status-and-log)). The
window shows the full text where the user is already looking.

A sketch (sizes and wording to be settled in VST3-11):

```text
+- Convolver 0.5.0  -  VST3 #2 in REAPER --------------------------------+
| Load failed: the filter is for 44100 Hz, the host runs at 48000 Hz.    |
|                                                                        |
| Request 3 (room.cfg) was refused, and the previous filter keeps         |
| playing. The configuration has entries for 44100 Hz only. Make or       |
| choose a filter for 48000 Hz, or set the project to 44100 Hz.           |
|                                                                        |
| Filter   [Choose filter...]  [Reload]   /Users/jrp/filters/room.cfg    |
| ---------------------------------------------------------------------- |
| Host       48000 Hz, 2 in -> 2 out (stereo), blocks up to 512 frames   |
| Filter     48000 Hz, 2 in -> 2 out, 2 paths, longest 65536 taps (1.4 s)|
| Latency    256 frames (5.3 ms)    Tail 65792 frames (1.4 s)             |
| Processing Accelerate, CPU kernels: baseline                           |
| Log        ~/Library/Logs/Convolver/Convolver.log                       |
|                                                                        |
| [Copy diagnostics]  [Show log file]                         [ ] Bypass |
+------------------------------------------------------------------------+
```

Every part of it, and where its content comes from:

| Part | Shows or does | Source (all on the control side) |
| --- | --- | --- |
| Title line | Version, format, instance tag (`VST3 #2`, as in the log) and host program, so a screenshot or copied report identifies the instance | `execution_line()` parts, the adapter's log tag |
| Status | The `HostStatus` name and `host_status()`'s line, then the full message: `HostFilter::failure()`, the speaker notice (`notice()`), and `fault_message()` for faults, each with its request number | `HostFilter`, `HostProcessor::faults()` through `observe()` |
| Instance not started | When the status is Not checked yet, a sentence saying the host has not started this instance at a rate and layout, and that some hosts (Audacity's real-time effects) show a separate copy in the window: trust the copy whose log says `Started at` | `host_status()`, the known host layout |
| Filter | Where the accepted filter came from: the file chosen in this session, or "saved project or preset" (state holds no path); the request number. **Choose filter...** opens the system's file dialog; **Reload** reads the same file again, for someone editing a filter between tries | `HostFilter::accepted()`, `HostRequest` |
| Host | Rate, channel counts, speaker arrangement, largest block | The adapter's known `HostLayout` and setup |
| Filter details | Rate, counts, number of paths, longest filter in taps and seconds, delays, and the path list (input to output, gain), shortened when long | The accepted `Configuration` |
| Latency and tail | 256 frames and their duration at the host rate; the tail the adapter reports | `HostProcessor::latency_frames()`, `tail_frames()` |
| Processing | FFT library and version, CPU kernels | `fft_backend_name()`, `fft_backend_version()`, `cpu_kernels_name()` |
| Gain (optional) | Estimated and worst-case peak gain on request, as the 2006 window showed after every load; a button rather than at every load, because the estimate costs a convolution of noise | `estimate_attenuation()` |
| Log | The log path, or why it cannot be written (`host_log_written()`); **Show log file** reveals it in Finder, Explorer or the file manager; **Copy diagnostics** puts the whole report and this instance's recent log lines on the clipboard, ready to paste into a report or to Claude | `host_log_path()`, the report text |
| Bypass | The host's Bypass parameter (VST3) or bypass property (AU), changed through the host so automation and undo see it | The format's own bypass |

The window adds no control that changes the sound. A gain or dry/wet control
is VST3-4's question, and the block size (latency) is PERF-2's.

**Copy diagnostics** is the debugging feature: one plain-text block, the same
in every format and system, made by one shared function, with the version,
format, host, status, full messages, request history of this instance, host
and filter details, FFT backend and kernels, and the last lines this instance
wrote to the log. Its wording reuses the log's sentences, so a report pasted
from the window and a log file never disagree.

## The 2006 VST2 window

`historical/convolverVST/convolverVSTEditor.cpp` (VSTGUI 3 over VST 2.4,
bitmaps from a third-party control set, revision of 27 June 2006) offered, top
to bottom:

| 2006 control | What it did | Keep? |
| --- | --- | --- |
| "Filter/config:" button and path label | `ConvolverVST::SelectFilter()` opened the host's VST2 file selector (`CFileSelector`) for a sound file or `.cfg` and rebuilt the convolution | **Keep**: Choose filter, with the path shown (truncated in the middle, as `kPathText` did), plus Reload |
| Status text | Before a load, help text with the website, an e-mail address and a credit for the control bitmaps; after a load, `ChannelPaths::DisplayChannelPaths()`: path count, "Stereo to 5.1"-style channels, rate, taps, lag in taps and seconds, estimated and peak gain | **Keep** the summary (now the filter details and optional gain) and a help line; link to the GitHub issues and the troubleshooting guide instead of the retired address |
| Attenuation slider and display (parameter 0, dB) | Output attenuation | **Drop**: no gain parameter exists; VST3-4 decides |
| Partitions slider and display (parameter 1; 0 = overlap-save) | Chose the partitioning and so the latency | **Drop**: the latency is fixed at 256 frames; PERF-2 decides any change |
| "Tuning rigour" menu (parameter 2) | FFTW planning effort | **Drop**: plug-in builds use Ooura or Accelerate, which have no planning (PLUG-1) |
| System-modal message boxes (`warn`, `abort`) | Every error and warning, including "Reload ConvolverVST" when the channel count changed | **Drop**: errors appear in the window and the log; nothing blocks the host |

Lessons from its implementation, which the threading rules below avoid:

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

The parameter numbers 0 to 2 stay free in the VST3 plug-in
([identities](../decisions.md#identities-and-file-formats)), so VST3-6 can
still map old projects; the editor does not use them.

## Framework choice

jrp asked for stability and longevity over features
([direction](../decisions.md#direction)), and the
[plug-in formats plan](2026-10-21-plugin-formats.md#development-frameworks)
chose each format's own headers over a framework, naming DPF as the fallback
"if a shared editor across formats is ever needed". The window is small
(about a dozen controls, mostly text), so the question is which costs less to
keep working for twenty years: three thin native views, or one toolkit.

| Option | Licence | Formats and systems | Accessibility | Longevity and fit | Verdict |
| --- | --- | --- | --- | --- | --- |
| **Native views** behind the format's own interface: `IPlugView` (VST3), an `AUCocoaUIBase` view factory (AU), the LV2 UI extension | Ours (GPL-2.0-or-later); no new dependency | AppKit `NSView` on macOS, shared by VST3 (`kPlatformTypeNSView`), the AU's Cocoa view and an LV2 `ui:CocoaUI`; Win32 `HWND` for VST3 and LV2 `ui:WindowsUI`; Linux X11 needs its own decision (below) | Standard AppKit and Win32 controls are exposed to VoiceOver and to UI Automation (NVDA, JAWS, Narrator) by the systems themselves | AppKit and Win32 are the oldest stable GUI interfaces on their systems; the code depends on nothing that can be abandoned. Costs one view per system (estimate 400 to 700 lines each), but the content is shared code | **Recommended** |
| VSTGUI 4.15 (Steinberg) | BSD-3-Clause ([Debian copyright file](https://metadata.ftp-master.debian.org/changelogs/main/v/vst3sdk/stable_copyright)) | Windows, macOS, Linux X11 (preliminary Wayland in 4.15, [3.8.0 notes](https://forums.steinberg.net/t/vst-3-8-0-sdk-released/1011988)); a VST3 editor class and WYSIWYG UI descriptions. Usable inside an AU Cocoa view or an LV2 UI only with glue of our own | None: "there is currently no support for accessibility in VSTGUI" (its maintainer, [November 2021](https://forums.steinberg.net/t/accessibility-with-vstgui/751087)); custom-drawn controls are invisible to screen readers | Maintained by Steinberg since 2003, released with the SDK we already pin, but a fifth SDK submodule (`vstgui4`) and a shipped dependency (with Linux libraries such as cairo, xcb and fontconfig, to be confirmed). Follows Steinberg's priorities | Second choice, mainly for Linux |
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
parameter. Neither drawn option is accessible. This plan defers the choice to
VST3-13, after the macOS and Windows windows exist and HOST-1 shows whether
Linux VST3 users need one.

## How the window connects

The window is a client of the control side and reuses what DIAG-1 and PLUG-8
built. Nothing in it is a second source of truth.

**A shared report (PLUG-9).** A new value type in `Convolver::host` (name to
be settled; `HostReport` here), built under the adapter's control lock from
`HostFilter`, the known `HostLayout`, the processor's latency, tail and fault
counts, the execution facts and the log state. It holds the status, the status
line, the full failure, notice and fault messages with request numbers, the
accepted request (source and path), host and filter details, the log path and
whether it is written. Two shared functions go with it:

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
- **The host is told the project changed**: VST3 `IComponentHandler2::setDirty`
  (and the existing `restartComponent(kParamValuesChanged)` for the Status
  text); AU a property-change notification for `kAudioUnitProperty_ClassInfo`;
  LV2 the `filter` parameter's `patch:Set`, as today.

## Threads

The audio thread is untouched: it plays what `HostProcessor` was handed and
signals faults as it does now. The rules for the window:

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
   completion handler; Windows' `IFileOpenDialog` is modal to the plug-in
   window only). The 2006 window's stall cannot recur, because the window
   never holds the control lock at all.
5. **Clipboard, log reading and "show file" run on the UI thread** and read at
   most the last 64 KiB of the log; they never write it.

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
  `notify()` loads it as `setState` does. Bypass is edited through the
  controller (`beginEdit`, `performEdit`, `endEdit` on parameter 1000).
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
- **Sandboxed hosts** (GarageBand): the system file dialog grants the host
  access to the chosen file only. A `.cfg` names other files, which the
  sandbox may refuse, so the message for an unreadable filter file inside a
  sandboxed host must say so and suggest a preset made with
  `convolver-preset` (which embeds the filters) or choosing the folder. To be
  checked in GarageBand during AU-6.
- The Audio Unit's licence (GPL-3.0-or-later with the AudioUnitSDK's notices)
  covers the shared view code, which stays GPL-2.0-or-later.

**LV2 (LV2-6, optional).**

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

Acceptance criteria for every window phase:

- Every control has an accessible name and role; the status and the full
  message are readable text that a screen reader reads in order and the user
  can select and copy (a read-only text view, not a drawn label).
- The window works from the keyboard alone: Tab order top to bottom, Space or
  Return on buttons, the file dialog reachable without a mouse. Hosts route
  keys differently (REAPER has "send all keyboard input to plug-in"; VST3
  hosts may use `IPlugView::onKeyDown`), so the host smoke tests check it.
- Status is never shown by colour alone: the status word is always written,
  and colours follow the system's appearance (dark mode, high contrast,
  increased contrast) because the controls are native.
- Text follows the system's text size where the platform has one, and the
  window resizes to fit rather than clipping.
- Checked with VoiceOver on macOS and NVDA or Narrator on Windows in a named
  host (REAPER is the DAW most blind users use, with the OSARA extension). The
  CI tests below check names and roles automatically.
- Linux drawn views cannot be reached by screen readers (AT-SPI comes from
  toolkits); if VST3-13 adds one, the documentation says so and points to
  LV2's generic UI and the log.

## Testing

Most of the window's behaviour is shared code and is tested without a
display, in the existing CTest targets and CI jobs:

| What | How | Where |
| --- | --- | --- |
| Report content and text | Table-driven: one case per `HostFailureKind`, notice, fault kind, Not checked yet, and log not writable, each checking `report_fields()` and `report_text()`; this can share the table DIAG-1 proposes for every surface | `native.host` |
| Load from a file equals the preset tool | For a WAV, a `.cfg` and a list, the state after a load from the window's path equals `convolver-preset`'s output byte for byte, and a refused file keeps the previous filter (PLUG-2 comparison of the audio) | `plugins.presets`, the PLUG-2 harness |
| VST3 messaging | Through the existing controller and processor in the test harness: `ConvolverLoad` loads, refuses with the right reason, calls `setDirty`, and the controller's report copy follows every load, activation and fault | `vst3.adapter`, `vst3.lifecycle` |
| AU properties | Report and load properties through `AudioUnitGetProperty`/`SetProperty`, with property-change events, under the same failure cases | `au.adapter` |
| LV2 atoms | The lilv test host reads the report atoms after loads and refusals; `run()` stays allocation-free under the audio guard | `lv2.adapter` |
| View opens and closes | Steinberg's `editorhost` (Linux under `xvfb-run`; built from the pinned SDK with its hosting examples) and pluginval without `--skip-gui-tests`, which opens the editor, also while processing | `vst3.pluginval`, a new `vst3.editorhost` |
| AU view | `auval` lists the Cocoa view and checks it loads ("Cocoa Views Available") | The `auval -strict` step of the AU workflow |
| Accessibility and content | A small test host per platform attaches the view to an off-screen window, walks the accessibility tree (`accessibilityChildren`, labels and roles on macOS; Win32 window text and `IAccessible` on Windows), checks the status text equals `report_text()`, presses Copy diagnostics and reads the clipboard, and saves a screenshot as a CI artifact for review (not compared pixel by pixel) | `vst3.editor`, `au.editor` |
| File dialog | Not automated: tests inject the chosen path through a test hook; the real dialog is a step in the host smoke tests | [host testing](../../develop/host-testing.md) |

The GitHub-hosted macOS runners have a logged-in desktop session, so views
open there; whether the Windows runners allow windows to be created and
measured is checked in VST3-12's first CI run. The host smoke tests in
[host testing](../../develop/host-testing.md#smoke-test-in-each-first-host)
gain steps for the window in REAPER, Audacity (including its separate
real-time copy) and GarageBand: open it, load a wrong-rate filter and read
the message, reload, copy diagnostics, show the log, and read it with
VoiceOver.

## Phases and TODO items

Each phase is its own pull request, follows the
[workflow](../../develop/workflow.md), and is useful without the next.

| Phase | Item | Delivers | Depends on |
| --- | --- | --- | --- |
| 1. Shared, no window | **PLUG-9** | `HostReport`, `report_fields()`, `report_text()`, and the preset tool's file-to-state step as a shared function; headless tests | jrp's choice of framework does not matter yet |
| 1 | **VST3-3** | Controller report copy, `ConvolverLoad`, `setDirty`, Bypass edits from the controller; headless tests. No `createView` yet | PLUG-9 |
| 1 | **AU-6** (first half) | Report and load properties with change events; headless tests | PLUG-9 |
| 2. macOS window | **VST3-11** | The AppKit view and its VST3 `IPlugView`; `editorhost`, pluginval GUI tests and the accessibility test on macOS CI; smoke steps in REAPER and Audacity; user guide section | VST3-3, framework decision |
| 2 | **AU-6** (second half) | The Cocoa view factory over the same view, versioned class names; `auval` view check; GarageBand smoke steps, sandbox check | AU-6 first half, VST3-11 |
| 3. Windows window | **VST3-12** | The Win32 view, content scaling, keyboard navigation, Windows CI view test | VST3-11 |
| 4. Linux | **VST3-13** | Toolkit decision, then the X11 view with `IRunLoop`, or a recorded decision not to have one | VST3-12, HOST-1 evidence |
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

The editor does not block the next milestone, an independent user running
the first preview; phase 1 can land before it and makes the log and Status
text no worse. Whether a preview waits for the macOS window is jrp's call.

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
  cannot repaint meanwhile. PERF-3 measures it; VST3-5 moves preparation to a
  worker if needed, and the window then shows "Loading".
- **Report size.** A list that matches nothing explains every candidate, which
  can be long. VST3 message attributes and AU dictionaries carry it easily;
  LV2's notify port needs a size declared up front (LV2-6).
- **Out-of-process Audio Units.** Hosts that run a version 2 unit in another
  process (for example under Rosetta) pass properties between processes;
  custom properties with Core Foundation values should survive that, which
  AU-6 checks in a host that does it.
- **Key handling and focus** differ by host; the smoke tests check that typing
  in the window does not trigger the host's shortcuts and vice versa.

## Decisions for jrp

Recommended answers first. None of these blocks phase 1.

1. **Framework:** native views (AppKit, Win32), no framework, with the content
   in shared code. Alternatives: VSTGUI for every system (one code base, no
   accessibility, a new shipped dependency), or DPF (replaces our adapters).
   This revises the formats plan's "DPF is the fallback for a shared editor":
   the shared part becomes our own report code.
2. **Order:** macOS first (VST3 and the Audio Unit share the view, and jrp's
   hosts are on his Mac), then Windows, then decide Linux; LV2's UI only if
   Linux users ask.
3. **Contents:** the window explains and loads (status and full messages,
   active configuration, copy diagnostics, show log, choose and reload a
   filter, bypass), with no sound-changing controls; the 2006 attenuation,
   partitions and planning controls are not brought back.
4. **The chosen path:** shown for the current session and written to the log,
   not saved with the project. Saving it for display would add VST3 controller
   state and an AU `ClassInfo` key, a format change.
5. **Accessibility:** the criteria above (screen reader names and order,
   keyboard use, no colour-only status) are acceptance criteria for each
   window phase, checked with VoiceOver in REAPER.
6. **Previews:** phase 1 lands when ready; the first preview does not wait for
   the macOS window.
