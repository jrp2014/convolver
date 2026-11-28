<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Release notes text

The words of the next release's notes that change from one release to the
next (REL-13). [`prepare_release.py`](../scripts/release/prepare_release.py)
reads this file and adds the rest: the download table, the guides, the
signing, the host tests from the evidence and how the release was built.
Each preview's release-notes pull request updates it
([releasing](../docs/develop/releasing.md#publishing-an-unsigned-preview)).

The script reads three sections by their headings. Each item is a list
entry, wrapped as you like: GitHub shows every line break in a release's
notes, so the script joins an item's lines into one. In an item, `{docs}`
stands for the tag's user guides (`https://github.com/OWNER/NAME/blob/TAG/docs/use`),
`{base}` for the repository's address and `{tag}` for the tag. A known
limitation that starts with `<!-- previews -->` is only in a preview's
notes, and one that starts with `<!-- full releases -->` only in a full
release's. The entry `<!-- generated -->` is where a full release lists
what its evidence leaves unsigned or not verified, and a preview nothing.
The script refuses any other comment or placeholder, and a section that
appears twice, rather than publish words nobody reviewed.
`python3 tests/release/prepare_release_tests.py` checks this file, and
pre-commit runs it whenever the file changes.

## Previous preview

0.18.29

## What changed

- For programs built on Convolver's C++ library: the installed package no
  longer includes the plug-ins' host layer, `convolver/host.hpp` and
  `Convolver::host`: `HostProcessor`, `HostFilter`, the host settings and
  gain helpers (`filter_gains()` among them), the saved-state codec, the
  routing and status helpers, all of `host.hpp`. The library is now
  `Convolver::core` and `Convolver::io`; `CHANGELOG.md` lists every removed
  name and what to use instead, and `docs/develop/library.md` in the source
  says how to build against the library.
- A 32- or 64-bit float Wave64 (`.w64`) file with an extensible header now
  reads correctly, as an input, as a filter and in the plug-ins. Until now
  its samples were read as integers, so such a filter played wrong with no
  message (a sample of 1.0 played as 0.496), and the 64-bit form did not
  open at all. An extensible A-law or µ-law Wave64 file, which played as
  noise, is now refused with advice to convert it to WAV first. A Wave64
  input with integer samples is no longer read from a pipe, since its
  header cannot be checked first: save it to a file, or send WAV or RAW
  audio.
- Convolver's window now heads a named preset with the preset's own name
  (`Stereo echo`), whether it came from the host's preset menu or from
  Choose Filter, and names the file it was made from in the file details;
  saving and reopening the project keeps the name, in every format. Choosing
  an IR file or `.cfg` directly shows that file's name, as before, and drops
  the preset's. Presets `convolver-preset` makes now carry their name:
  `--name`, or the preset's file name; a `--name` over 200 bytes, or with
  control characters, is refused before anything is written. In the Audio
  Unit, a preset saved through the host's own preset menu (GarageBand,
  Logic Pro) heads the window with the name of the preset that was loaded,
  or the filter file's, as in VST3. A project or preset holding a preset's
  name cannot be opened by 0.18.29 or an earlier preview (see Known
  limitations).
- The Gain mode choices are now Manual, Estimated and Clip-safe (were Set,
  Estimate and No clipping), in the window, the host's own controls, Status
  and the log; saved projects and presets open unchanged. Gain mode
  Estimated, and `--gain-db auto` on the command line, find their figure
  up to three or four times sooner for long filters (about 1.3 s instead of 4.1 s
  for a 30-second stereo filter on a test machine), and the figure changes
  only by rounding.
- The window keeps its height, and its controls their places, when a
  message comes, goes or changes; the Gain and Mix sliders, their value
  boxes, the status word and the filter's facts have tooltips; most of the
  Gain slider's travel is between −30 and +12 dB; the window says which
  file Reload When the File Changes watches; a kept filter that does not
  suit the host shows Waiting in orange; Worst-case peak for a long reverb
  says music rarely reaches it and points to Gain mode Estimated; and on
  Windows, Ctrl-click on the Gain or Mix slider puts its default back, as
  Option-click does on macOS.
- On macOS, the window's captions, labels and notes, and a problem's box,
  stay readable in a dark host window on a light Mac (and the other way
  round), and after switching between Light and Dark while it is open; with
  no filter loaded, the blank space for the filter's facts shows no
  tooltip. On Windows, the status word's tooltip says what the status
  means (it was empty); Ctrl-click on a slider while a value typed in its
  box is not yet set resets the setting as one edit; a window reopened
  after a refusal was fixed has no blank gap where the refusal was; and
  closing the window removes its tooltips.
- In VST3, after a host restores a project or preset during an offline
  render, for example, the host's first read of Gain, Mix, Gain mode,
  Latency and Bypass gives the restored values, not those from before. A
  project playing a named preset that kept no filter file name no longer
  saves another filter's name (from the project reopened before, or from a
  preset just refused).
  In the Audio Unit, such a preset saved through the host's preset menu
  under another name no longer shows that name as its filter file.
- The window's folded details and Copy Diagnostics give a line for each
  output, with the inputs that reach it, their route gains and the delays
  (`Out 3: (In 1 + In 2) ×0.5, output delayed 48 frames (1.0 ms)`), so every
  path of a crossover shows, and say what plays on an output no path feeds.
- A four-channel IR file marked as Ambisonics B-format cannot be used as
  true stereo on a stereo track, or on stereo audio on the command line,
  and the message says why, wherever the marking is: in an RF64 or
  Wave64 file, and in a WAV whose format comes after many other chunks,
  too. Until now a file marked there played as true stereo with the wrong
  routing, `--inspect` called it true stereo, and `convolver-preset` saved
  that routing without a warning: make such a preset again. On four
  channels it plays as before. The command-line tools no longer also
  suggest omitting `--output-channels` for it, which gave the same
  refusal, and a four-channel IR file asked for 2 inputs and 4 outputs is
  told that true stereo has two outputs.
- In a host whose process follows a German or French number format,
  numbers in the window, Status, the report, the log and messages now use a
  decimal point (the Gain box read `-6,0`), and on macOS the dimmed Gain
  slider in Estimated or Clip-safe sits at the right place.
- On macOS, Option-clicking a slider resets it even when Option is let go
  before the mouse button, as one change. In the Audio Unit, changing
  Latency while Status shows Load failed or Waiting no longer shows Fault
  in its place, and a slider drag is one change for the host's automation
  and undo, as in VST3.
- Restoring, saving and choosing a filter do less work: saving no longer
  copies the filter or holds up the controls, a VST3 restore no longer
  decodes the filter twice, the plug-ins find a new filter's worst-case
  peak and noise gain in one pass, and a mono filter playing on each
  channel is no longer copied for every channel when the host restores it.
  Nothing heard changes, and the saved bytes are the same.
- The Convolver log's line after an accepted load reads the same in the
  VST3 plug-in and the Audio Unit: VST3 now lists the settings restored with
  a project or preset, and the Audio Unit says what plays while a filter
  that does not suit the host waits, and that it is checked again.
- On macOS and Linux, an input or a filter can come from a pipe named by
  a path, such as the shell's `<(command)`, `/dev/stdin` fed by a pipe or a
  named pipe. Until now, on Linux, the command line stopped with "cannot
  make canonical path", and a configuration, list or FLAC filter from a
  pipe was refused with misleading messages. A filter from a pipe is read
  whole first, so a configuration, a
  configuration list, a FLAC IR and an IR that plays on each channel or as
  true stereo load as they would from a file; a configuration from a pipe
  must name its files with full paths. An input that cannot be read from a
  pipe, such as FLAC, says that the pipe is the reason and what to send
  instead. A file redirected to `/dev/stdin` is read by its own path, so a
  configuration given so finds its IR files beside it, and macOS no longer
  refuses such a WAV file.
- When `convolver-preset` stops part-way through writing a preset, on a
  full disk for example, it removes the unfinished file as well as the
  presets it had already made in that run, so the same command works once
  the problem is fixed; `convolver --tune` and `convolver-equalizer-apo`
  remove an unfinished file too. Until now the unfinished file stayed and a
  retry was refused because it existed.
- On Windows, when an output path is too long for Windows (a file's over
  259 characters, a folder's over 247), the command-line tools say so and
  give its length, instead of blaming the folder's permissions.
- The release files no longer say "preview" in their names
  (`Convolver-X.Y.Z-<platform>.zip`).

## Known limitations

- On Linux the VST3 plug-in has no window yet. Load a filter from the host's
  preset menu (each example comes with a `.vstpreset`, and
  `convolver-preset` makes one from your own filter) and set Gain, Mix and
  Latency in the host's own controls ([load a
  preset]({docs}/first-sound-in-a-host.md#optional-load-a-preset)).
- The LV2 plug-in (Linux) has no window: choose a filter
  with the host's file chooser for its Filter parameter, where the host
  shows one, or load an LV2 preset made with `convolver-preset` ([plug-in
  presets]({docs}/presets.md)), and set Gain, Mix and Latency with the
  host's own controls. Each channel layout is its own plug-in. It was
  checked automatically, with lilv, lv2lint, pluginval and headless Ardour 9
  sessions, and from a source build in PipeWire 1.6.8 on virtual devices;
  nobody has used it in a desktop host yet. In hosts built on JUCE,
  pluginval among them, closing the plug-in waits up to 100 ms for a queued
  Latency, Gain mode or fault report. Since 0.13.24 this mitigates a host
  crash rather than ruling it out: a file request the host works after
  closing, or a request delayed over 100 ms by another plug-in's work, can
  still reach the closed plug-in and crash the host.
- <!-- previews --> The Windows window has been checked only automatically,
  on Windows CI machines: it opens, and the plug-in validator's window tests
  pass. Nobody has tried it in a Windows host or with a screen reader yet.
- <!-- full releases --> The Windows window has not been tried with a screen
  reader yet.
- <!-- generated -->
- <!-- previews --> Unsigned: macOS asks you to confirm that you want to
  open it, and Windows shows a SmartScreen warning.
- Windows needs the Microsoft Visual C++ 2015-2022 x64 runtime, which most
  computers already have.
- <!-- previews --> An early preview, published before its last checks. Its
  new audio engine, which adds no delay at Latency 0, has been checked
  automatically, against an exact reference and the command-line tool, on
  macOS, Windows and Linux, and in Audacity exports on macOS; nobody has
  listened to it in a host yet. With the previous engine (0.12.12), on one
  Mac, renders and exports from REAPER and Audacity (VST3) and GarageBand
  (Audio Unit) matched the command-line tool, and saved projects reopened
  with their filter. Not yet checked: listening in any host, VoiceOver, the
  latest Audio Unit changes in GarageBand, and anyone outside the project
  following the guides. Checked on 0.18.10, built from its tag on one Mac
  (macOS 27.2.1, 22 November 2026), and not yet repeated on this version: in
  REAPER 7.86, the render, save and reopen from one command, stereo and
  six-channel renders matching after reopening, the ring-out kept through
  Stop, a 30-second filter at Latency 0 without underruns, and a project
  saved with 0.14.0 rendering the same in VST3 and the Audio Unit; in
  Audacity 4.0.2, a filter chosen with Choose… before playing; in GarageBand
  10.4.15, exports aligned exactly at Latency 0 and within 1.2e-7 at 1024.
  To retest on this preview, with what 0.18.10 showed where it was tried: in
  REAPER, a refused preset while playing (0.18.10 refused it correctly but
  showed the wrong settings, fixed in 0.18.12) and the wrong host rate; in
  GarageBand, presets, bypass, settings, a Latency change stopped and while
  playing, and the recent files and Reload in the Choose Filter menu; in
  Audacity, a configuration list chosen before playing (failed on 0.18.10,
  fixed in 0.18.12), the recent files, and save and reopen. The Windows and
  Linux packages were checked only automatically, on CI machines that had
  never built Convolver: the tools and examples ran from them, the plug-in's
  library loaded and listed its classes, and the Windows installer installed
  and removed itself, on Windows Server 2025 and on Windows 11 on Arm, where
  the package ran under the x64 emulation built into Windows (GitHub has no
  Windows 11 machine for x86-64). Nobody has used them in a host yet. The
  macOS packages were also installed and removed on such machines with macOS
  15, the oldest GitHub has; nothing has run on macOS 14 itself.
  [Status]({base}/blob/{tag}/docs/project/status.md#checks-left-before-publication)
  lists what is open. Other hosts are untested.
- In Audacity, a filter chosen with Convolver's Choose File…, even before
  playing, is kept for exports and saved projects, and one chosen from its
  recent files for exports (checked in Audacity 4.0.2 on macOS on 21
  November 2026 with the 0.16.14 candidate, whose window had separate
  Choose… and Recent buttons; Choose… before playing was checked again on
  0.18.10 on 22 November; not yet repeated on this version; Audacity's own
  preset menu was last checked on 7 November 2026, with a build before the
  first preview). Reload and reloading when the file changes are checked
  only by automated tests.
- In Ardour, the VST3 plug-in on a Flexible-I/O track gets 2 inputs and 2
  outputs whatever the track has; a mono filter plays on each of them
  (checked by automated tests, not yet in Ardour's window), but a crossover
  cannot play; nor can a crossover on a Strict-I/O track, Ardour's default,
  in either format. On Linux use the LV2 plug-in on a Flexible-I/O track for
  a crossover ([Ardour]({docs}/plugins.md#ardour)).
- Checked only by automated tests: new since 0.18.29, everything under What
  changed; new in 0.18.29, the changes [its
  notes]({base}/releases/tag/v0.18.29) list, including the new way the VST3
  plug-in's two halves keep in step, which every VST3 check above predates;
  new in 0.18.12, the changes [its notes]({base}/releases/tag/v0.18.12)
  list; new in 0.18.10, the changes
  [its notes]({base}/releases/tag/v0.18.10) list, apart from those checked
  on 0.18.10 above and the Audacity routes checked on the 0.16.14 candidate;
  new in 0.14, the quicker restore of the playing filter and the saving once
  the sound has stopped; new in 0.13.20, the Gain mode, a `.vstpreset`
  chosen in the window, choosing a file on a track with more inputs than the
  filter's, and a preset or undo that keeps the playing filter.
- Since 0.13, Latency starts at 0 frames. Some projects saved with 0.12.12
  at its default of 256 frames open at 0; choose 256 again if you want its
  delay back ([projects from earlier versions]({docs}/plugins.md#settings)).
- Opening a project in an earlier preview: in VST3, a project or preset
  saved with this version at Latency 0, the default, is refused as damaged
  by the first preview, 0.12.12, which plays silence and says so in its log,
  and one saved with a Gain mode other than Manual is refused by every
  preview before 0.13.20. In the Audio Unit and LV2, previews before 0.13.20
  have no Gain mode, so they play such a project at its Gain setting, which
  can be louder. In every format, a project or preset that holds a preset's
  own name, which includes every preset `convolver-preset` now makes and any
  project whose filter came from one, is refused by every preview up to
  0.18.29 as saved by a newer version; a project whose filter was chosen
  straight from a file still opens there. Projects and presets saved with
  earlier previews open in this one.
