<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in windows on Windows and Linux

For jrp, who chooses the route, and for whoever then builds VST3-12 (the
Windows window) and VST3-13 (the Linux window). It revisits the choices of
24 and 25 October 2026 with what the macOS window has since shown, compares
the routes, and recommends an order. It changes no code or workflow. jrp chose
the recommended route, C, on 10 November 2026
([decision](../decisions.md#direction)).

**In short:** whichever toolkit is chosen, two pieces of work come first: the
Windows and Linux plug-in needs a sound-file reader, since without one a window
could not load a filter, and the window's behaviour, now written in
Objective-C, should move into shared C++ so each system's window is only its
controls. The recommendation is then the native Win32 window on Windows, as
decided, and the Linux VSTGUI window only after LV2 ships on Linux and if
Linux VST3 users ask for it. Contents: [where things
stand](#where-things-stand), [what comes first](#what-comes-first),
[routes](#routes), [the Win32 window](#the-win32-window), [the Linux VSTGUI
window](#the-linux-vstgui-window), [recommendation](#recommendation),
[steps](#steps), [decisions for jrp](#decisions-for-jrp).

## Where things stand

- **The macOS window** (VST3 and the Audio Unit) is native AppKit in
  [`plugins/editor/mac/`](../../../plugins/editor/mac/) with the VST3 glue in
  [`plugins/vst3/editor.mm`](../../../plugins/vst3/editor.mm): about 2,100
  lines (the view about 1,080, the setting rows 356, the rest headers, layout, file
  watching and the VST3 glue), against the 400 to 700 the
  [editor plan](../../archive/plans/2026-10-24-plugin-editor.md#framework-choice)
  estimated per system. Everything it says about the filter and the audio
  comes from the shared report (`portable/host_report`), but its behaviour is
  Objective-C: which rows and messages show for a report, reading a typed
  value (decimal comma accepted), resetting a setting, the Choose… request
  that a closed window cancels, the Recent menu, the once-a-second reload
  check and reading the report only while the window is shown.
- **The Windows and Linux VST3 plug-in reads no sound files.** On macOS a
  chosen filter is read with AudioToolbox; elsewhere the load is refused
  (`test_file_reader` in
  [`plugins/vst3/processor.hpp`](../../../plugins/vst3/processor.hpp)). The
  tools and the LV2 plug-in read with libsndfile, which the package build
  compiles from source and already ships with its licence notice.
- **Decided:** native views with no framework, a Win32 view on Windows, and
  VSTGUI for the Linux VST3 window only, as a stop-gap; accessibility highly
  desirable but not essential
  ([decisions](../decisions.md#direction), 24 and 25 October 2026). Both
  windows moved to later previews on 6 November 2026, and runtime changes are
  frozen until preview feedback arrives
  ([decisions](../decisions.md#releases)).
- **Nobody on the project has a Windows or Linux computer.** Every check is
  CI or a tester's report, so a route that CI can drive counts for more than
  it did for the macOS window.

## What comes first

Both are needed by every route below, and neither changes what a user sees on
macOS.

1. **A sound-file reader for the Windows and Linux plug-in.** Proposed:
   libsndfile linked into the plug-in, built as the packages already build it.
   The plug-in then reads exactly what the command-line tool in the same
   package reads: WAV, RF64, W64, AIFF, CAF and raw files, but no FLAC or Ogg,
   since the packages build libsndfile without its external codecs
   ([what a package reads](../../use/configuration.md)). It handles non-ASCII
   Windows paths already (`sf_wchar_open` in
   [`portable/io.cpp`](../../../portable/io.cpp)) and is already in the
   packages and their licence notices. Windows' own Media Foundation was
   considered, as AudioToolbox was chosen on macOS so that the Audio Unit links
   nothing beyond the system. It reads FLAC, which the macOS window also reads,
   but not AIFF, CAF or raw files, so a filter the command-line tool reads could
   be refused by the plug-in on Windows only. Either way, Windows and macOS
   would differ in one format family; libsndfile keeps each package's tool and
   plug-in in step. Linking libsndfile into the plug-in is a change to what
   ships inside it, so it is jrp's choice ([below](#decisions-for-jrp)).
2. **The window's behaviour in shared C++** (proposed PLUG-34), in
   `portable/window_model.hpp` (built into `Convolver::host` beside the
   recent-files list) with no user-interface code: the rows and
   messages to show for a report and the text of each value, typed values,
   reset, the Choose… request token, the reload check (the logic of
   `file_watch.mm`, on `std::filesystem`), the Recent list, and when to read
   the report. The macOS view then calls it with no visible change, its
   `vst3.editor` and `au.editor` tests unchanged, and the shared part gains
   tests that run on every CI system, Linux included. Each new window is then
   controls and layout only, and the three windows cannot drift apart in what
   they do. This is the case TODO's
   [execution priorities](../../../TODO.md#execution-priorities) allow for
   moving code into a shared layer: a second system needs the same behaviour.
   It changes shipped macOS code, so it waits for the freeze to lift like the
   windows.

## Routes

| Route | Windows | Linux | Window code to keep | New shipped dependency | Screen readers | CI can drive it |
| --- | --- | --- | --- | --- | --- | --- |
| **A. As decided** | Native Win32 | VSTGUI | Three (AppKit, Win32, VSTGUI) | VSTGUI and about 13 desktop libraries, Linux only | macOS and Windows | Windows through UI Automation; Linux by screenshots under `xvfb` |
| **B. VSTGUI for both** | VSTGUI | VSTGUI | Two (AppKit, VSTGUI) | VSTGUI on Windows and Linux | macOS only | Screenshots on both; no control tree to query |
| **C. Windows first, Linux later** | Native Win32 | Host controls now; A's VSTGUI window later if asked for | Two now | None now | macOS and Windows | Windows through UI Automation |

Set aside again, for the reasons in the
[editor plan](../../archive/plans/2026-10-24-plugin-editor.md#framework-choice),
which still hold: a hand-drawn X11 view (no more accessible than VSTGUI, and
every control ours), GTK or Qt inside a host (a second toolkit version in the
host's process is a known crash cause), a web view, DPF (replaces our
adapters) and JUCE (licence); [JUCE and iPlug2](#juce-and-iplug2) are weighed
below.

### JUCE and iPlug2

jrp asked on 10 November 2026 whether a framework such as JUCE or iPlug2
would help. Both were checked against their current source that day.

| | JUCE 8 | iPlug2 |
| --- | --- | --- |
| Licence | AGPLv3 or a paid commercial licence. AGPLv3 can be combined with our GPL-2.0-or-later code only under GPLv3 terms, so the packages would no longer be GPL-2.0 compatible: a licence change for jrp | WDL licence (permissive, zlib-like); compatible |
| Screen readers | Yes on macOS and Windows (its own accessibility layer); none on Linux | None: controls are drawn (NanoVG or Skia) |
| Formats | VST3, AU, AUv3, LV2, AAX, standalone | VST3, AU, AUv3, CLAP, AAX, web, standalone; no LV2 |
| Linux | Plug-ins and windows, with a file dialog through zenity or kdialog, like VSTGUI | No supported Linux plug-in builds |

What a framework would save is the per-system window code: one window
instead of AppKit plus Win32 (plus VSTGUI). What it costs is larger than that
saving, because both own the whole plug-in, not only the window. Using either
as intended means replacing our VST3, Audio Unit and LV2 adapters with its
wrappers, then redoing the adapter contract and its lifecycle tests, and first
proving that the wrappers keep Convolver's permanent class IDs, AU codes, LV2
URIs and saved state, so that projects saved with 0.12 and 0.13 still open
(COMPAT-1 checks exactly this). A JUCE window alone inside our own adapters is
possible (a JUCE component attached to the host's window), but JUCE then also
needs its message loop driven from each host's run loop, which its own
wrappers do for it, and the licence question stays. Of the two, only JUCE
would give accessible windows, and only behind the licence change; iPlug2 is
no more accessible than VSTGUI and drops LV2. Neither changes the
recommendation; they stay set aside, as in the
[editor plan](../../archive/plans/2026-10-24-plugin-editor.md#framework-choice)
and the formats decision of 21 October 2026.

Rough sizes, once the shared behaviour is in place: the Win32 window about
1,200 to 1,600 lines, since Win32 has no automatic layout; the VSTGUI window
about 800 to 1,200. These are estimates from the macOS code, not
measurements.

## The Win32 window

What VST3-12 involves, from Microsoft's documentation and the VST3 SDK:

- **Embedding.** The editor is the SDK's `CPluginView` for
  `kPlatformTypeHWND`, as `editor.mm` is for `kPlatformTypeNSView`: a child
  window of the host's, with standard controls (labels, buttons, check boxes,
  trackbars, edit boxes, combo boxes, and a read-only multi-line edit box for
  the message).
- **Common Controls 6.** A DLL gets the current control styles only through
  its own activation context (a manifest resource and
  `ISOLATION_AWARE_ENABLED`); without it the controls fall back to the
  Windows 2000 look.
- **Screen scaling.** The host process sets its own DPI awareness, so the view
  creates its window under `SetThreadDpiAwarenessContext`, lays out from the
  font's metrics, and handles `IPlugViewContentScaleSupport` and
  `WM_DPICHANGED`.
- **Layout by hand**, in rows like the macOS form, with the same spacing
  (`view_layout.h` is AppKit-only, so its measures move to the shared part).
- **Keyboard.** `WS_EX_CONTROLPARENT` and `IsDialogMessage` give Tab and the
  arrow keys between controls; some hosts take keys first, which only testers
  in those hosts can show (the same open risk as on macOS, recorded in
  HOST-2).
- **The rest:** `IFileOpenDialog` for Choose… (cancelled when the window
  closes, as on macOS), `WM_DROPFILES` for a dropped file, `SetTimer` for the
  report and reload checks, the clipboard for Copy Diagnostics, Explorer for
  Show Log, and UTF-8 to UTF-16 at the edges.
- **Light only.** Win32's standard controls have no supported dark theme, so
  the window keeps the system's light colours in a dark host.
- **Accessibility.** Standard controls appear to Narrator, NVDA and JAWS
  through UI Automation; each slider and box is named by the label before it.
  A Windows CI test can open the view, read the same tree a screen reader
  reads, press its buttons and keep a screenshot, which is the nearest thing
  to a host check the project can run itself.

## The Linux VSTGUI window

What VST3-13 involves, checked against VSTGUI's current source on 10 November
2026:

- **Embedding.** VST3 on Linux embeds an X11 window
  (`kPlatformTypeX11EmbedWindowID`) and runs on the host's `IRunLoop`, which
  VSTGUI's VST3 editor supports. VSTGUI would be a fifth VST3 SDK submodule
  (`vstgui4`, BSD-3-Clause), built into the Linux plug-in only.
- **Libraries.** VSTGUI's build requires xcb, xcb-util, xcb-cursor,
  xcb-keysyms, xcb-xkb, xkbcommon, xkbcommon-x11, GLib, cairo, Pango
  (pangocairo and pangoft2), fontconfig and FreeType, and its development
  branch adds Wayland's client libraries. They are linked dynamically: every
  desktop has them, but the plug-in would then fail to load at all on a system
  missing one, the clean-machine containers would need them installed, and
  the install guide would list them. Keeping the window in a second library
  that the plug-in loads only when a window opens avoids that, at the cost of
  more build and package work.
- **File dialog.** On Linux VSTGUI's file selector runs `/usr/bin/zenity` or
  `/usr/bin/kdialog`; with neither installed, Choose… cannot open a dialog
  (dropping a file and Recent still work).
- **No screen readers.** VSTGUI has no accessibility support; the guide would
  say so.
- **Drawn controls.** The window draws its own controls in a palette we
  choose, so it does not follow the desktop's theme.

Linux users also have the LV2 plug-in, whose hosts' generic controls already
offer a file chooser for the filter, once LV2-5 ships it in a package.

## Recommendation

Route C: the shared behaviour and the reader first, then the Win32 window,
and the Linux VSTGUI window only after LV2 is in a Linux package and Linux
VST3 users ask for a window.

- Windows is where the old Convolver's users are, and a native window keeps
  Narrator, NVDA, the keyboard and Windows' scaling for free, which is why
  native views were chosen on 24 October.
- The Linux stop-gap costs about as much as it did, plus what the source
  check above found (a dozen desktop libraries the plug-in would not load
  without, and an external program for the file dialog), while LV2 will give
  Linux users a filter chooser without it. Deferring it is a change of order,
  not of the 25 October choice: if it is built, it is still VSTGUI.
- Route B saves writing one window, but Windows users would lose screen
  readers, and VSTGUI would ship on Windows too: the trade the 24 October
  decision declined.

## Steps

Each is its own pull request under the
[workflow](../../develop/workflow.md), after the freeze lifts or jrp says
otherwise:

| Step | Item | Delivers | Checked by |
| --- | --- | --- | --- |
| 1 | **PLUG-34** (new) | The window's behaviour in `portable/window_model.hpp`, used by the macOS window with no visible change | Its tests on every CI system; `vst3.editor` and `au.editor` unchanged |
| 2 | **VST3-12**, first part | libsndfile in the Windows and Linux plug-in; a filter loads through the controller there; the comments that tie the reader to the windows (`processor.hpp`, the VST3 `CMakeLists.txt`) updated | `vst3.adapter` loading files without `test_file_reader`; package and licence checks |
| 3 | **VST3-12** | The Win32 window, with the user guide and release notes updated | A Windows CI test through UI Automation with a screenshot; pluginval with its editor tests; a tester in REAPER and one other Windows host |
| 4, if asked | **VST3-13** | The VSTGUI window for Linux | `editorhost` under `xvfb-run` with a screenshot; clean-machine containers with the libraries; a tester in REAPER or Bitwig |

## Decisions for jrp

1. **The route:** C, chosen by jrp on 10 November 2026
   ([decision](../decisions.md#direction)).
2. **libsndfile inside the Windows and Linux plug-in**, rather than Media
   Foundation on Windows and nothing on Linux: chosen by jrp on 10 November
   2026 ([decision](../decisions.md#licences-and-dependencies)). It is already in the packages
   and their notices; the change is that the plug-in links it. The trade-off:
   with libsndfile the Windows plug-in reads AIFF, CAF and raw files but not
   FLAC (as the packaged tool); with Media Foundation it reads FLAC (as the
   macOS window does) but not AIFF, CAF or raw files.
3. **Only if the Linux window is built:** whether the plug-in may link the
   desktop libraries directly (simpler; fails to load without them) or keep
   the window in a library loaded when it opens (loads anywhere; more work).
