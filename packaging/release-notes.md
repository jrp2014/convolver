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
`python3 tests/release/prepare_release_tests.py` checks this file, and
pre-commit runs it whenever the file changes.

## Previous preview

0.18.12

## What changed

- A mono IR file, or any filter with one input and one output, now filters
  every channel of stereo or wider audio on its own, and a four-channel IR
  file on stereo audio plays as true stereo (left to left, left to right,
  right to left, right to right), without a `.cfg`, as other convolution
  reverbs read these files. A filter that matches the channels exactly still
  wins, in a list too, so existing setups play as before; a file marked as
  Ambisonic B-format is refused with the reason. Status, the log, the
  window's Routing line and the command line say when such a routing plays.
  In Ardour, a mono filter now plays with the VST3 plug-in on a Flexible-I/O
  track without setting its channels by hand.
- In VST3, host presets, window presets and later Latency, Gain mode, Gain
  and Mix changes now play, show and save the same in every order of host
  calls the plug-in's tests try, delivered on time or late: the plug-in's
  two halves ask each other directly for settings instead of passing them in
  messages that a host could deliver late or in the middle of a restore, and
  each window pairs only with its own instance's audio half, even when the
  host passes the pairing message late. A Latency or Gain mode chosen just
  before a preset is restored no longer overrides that preset when the host
  delivers it late, which could play and save the earlier Latency while the
  window showed the preset's. Settings changed after a refused preset no
  longer jump back when the host answers late, a window preset no longer
  comes back over a later edit or restore, a refused preset no longer leaves
  the window showing its Bypass or Status showing Active, and a host that
  restores the window's half first, or the same state twice, reads the
  latency that plays. No change to how a filter sounds or to the
  saved-project format.
- A valid VST3 project or preset is no longer refused as "could not be read:
  std::bad_alloc" when memory runs short, and restores are quicker,
  including the one Audacity makes at every playback start, because they no
  longer make a second copy of the filter. On Windows, running out of memory
  while loading a filter could leave the plug-in not responding; it now
  reports the failure.
- When a host offers the VST3 plug-in a channel layout it cannot take, as
  REAPER does for every instance with 64 inputs and outputs, the Convolver
  log now says the plug-in declined the offer, and that the next "Started
  at" line gives the layout in use, instead of a "Refused" line that looked
  like a fault.
- On Windows, the note under the Gain row in Estimated or Clip-safe uses the
  small type of the window's other notes, so its lines can no longer be cut
  off.
- Gain mode Estimated, and `--gain-db auto` on the command line, now give a
  figure for every filter that Clip-safe can play, including one with taps
  near the largest number a float holds, the same in every build, so the
  plug-ins' Status note `Too loud to estimate` can no longer happen and is
  gone. Ordinary filters' figures are unchanged, bit for bit.
- Convolver's window now heads a named preset with the preset's own name
  (`Stereo echo`), whether it came from the host's preset menu or from
  Choose Filter, and names the file it was made from in the file details;
  saving and reopening the project keeps the name, in every format. Choosing
  an IR file or `.cfg` directly shows that file's name, as before, and drops
  the preset's. Presets `convolver-preset` makes now carry their name:
  `--name`, or the preset's file name.
- The README's new What to expect section says, for room correction and
  crossovers, music at each track's own sample rate and a reverb on a send,
  what works today, the measured load and what you do.

## Known limitations

- On Linux the VST3 plug-in has no window yet. Load a filter from the host's
  preset menu (each example comes with a `.vstpreset`, and
  `convolver-preset` makes one from your own filter) and set Gain, Mix and
  Latency in the host's own controls ([load a
  preset]({docs}/first-sound-in-a-host.md#optional-load-a-preset)).
- <!-- previews --> The LV2 plug-in (Linux) has no window: choose a filter
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
- <!-- full releases --> The LV2 plug-in (Linux) has no window: choose a
  filter with the host's file chooser for its Filter parameter, where the
  host shows one, or load an LV2 preset made with `convolver-preset`
  ([plug-in presets]({docs}/presets.md)), and set Gain, Mix and Latency with
  the host's own controls. Each channel layout is its own plug-in. It was
  checked automatically, with lilv, lv2lint, pluginval and headless Ardour 9
  sessions, and from a source build in PipeWire 1.6.8 on virtual devices. In
  hosts built on JUCE, pluginval among them, closing the plug-in waits up to
  100 ms for a queued Latency, Gain mode or fault report. Since 0.13.24 this
  mitigates a host crash rather than ruling it out: a file request the host
  works after closing, or a request delayed over 100 ms by another plug-in's
  work, can still reach the closed plug-in and crash the host.
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
- Checked only by automated tests: new since 0.18.12, everything under What
  changed, including the new way the VST3 plug-in's two halves keep in step,
  which every VST3 check above predates; new in 0.18.12, the changes [its
  notes]({base}/releases/tag/v0.18.12) list; new in 0.18.10, the changes
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
