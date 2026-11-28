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
release's. The entry `<!-- generated -->` is where the script lists
signing limitations and, for a full release, outstanding verification.
The script refuses any other comment or placeholder, and a section that
appears twice, rather than publish words nobody reviewed.
`python3 tests/release/prepare_release_tests.py` checks this file, and
pre-commit runs it whenever the file changes.

## Previous preview

0.19.21

## What changed

- The command line now picks the output's file type from its name when
  `--output-format` is not given: `out.flac` writes a 24-bit FLAC file
  (FLAC has no float type), and `.rf64`, `.w64`, `.aif`, `.aiff`, `.aifc`,
  `.caf`, `.raw` and `.pcm` write 32-bit float in that type. A `.wav` name,
  or a name without a known extension, still gives 32-bit float WAV, as
  before. Until now `out.flac` was a WAV file with the wrong name. When
  `--output-format` names another type than the file's name, the file is
  written as `--output-format` says, with a warning. A build without FLAC
  refuses an output named `.flac` before creating anything.
- `--gain-db no-clip` and `--gain-db auto` now report their figure as
  `no-clip gain` and `optimum gain` (were `no-clip attenuation` and
  `optimum attenuation`), since it is a gain as `--gain-db` takes it: a
  negative figure lowers the level. The figure and the output are
  unchanged.
- A filter that plays as silence, because every tap of its impulse
  responses is zero or too small to count, now gets a warning on the
  command line and in the VST3 plug-in and the Audio Unit: their Status
  says "The filter plays as silence", and the window and the Convolver log
  say why and what to check. It still loads and plays, silently. Gain mode
  Estimated and Clip-safe, and `--gain-db auto` and `no-clip`, give such a
  filter 0 dB; a filter of tiny taps was given hundreds of decibels before.
- With `--gain-db no-clip` and float output, a sample that the
  convolution's rounding takes a hair over full scale is no longer put
  down to input beyond full scale; the note says it can be that rounding
  and that 0.001 dB more keeps it within full scale.
- On Windows, Convolver's window gives the tooltips the macOS window gives
  (a long filter name or speaker list in full, the filter's taps, and what
  each Audio fact means), and the status word is in the ordinary text
  colour with the colour on its dot, as on macOS. Clicking the Gain or Mix
  slider after typing in its box, without pressing Return, is now one
  change; it set the typed value first, or beeped at text that is not a
  number.
- In VST3, a project or preset that holds a Latency the plug-in could not
  apply before (out of memory, say) tries it again when it is restored, as
  the Audio Unit does. A host that passed a control value that is not a
  number could crash the plug-in when it asked for Latency's or Gain mode's
  text, and Gain's and Mix's text read "nan"; such a value now counts as
  the control's default, as in the Audio Unit. With a host that joins the plug-in's two halves on another thread
  than its window's, Gain, Mix and Bypass could show values the plug-in was
  not playing until the next restore; they now keep in step.
- In the Audio Unit, switching bypass while the plug-in was busy (applying a
  Latency or Gain mode change, say) could leave the host's Status and
  Convolver's window showing the old bypass setting when no filter was
  playing; and a filter chosen in the window now tells the host about the
  change on its main thread, as the plug-in's other notices do.
- If the plug-in could not get ready to play when the host started it, and
  memory then ran out while Convolver recorded why, its Status could read
  Active although nothing played; it now says Load failed.
- When a filter is refused only for its sample rate, the message now says
  the file needs audio at its rate instead of describing its channels.
  `convolver --validate` names the filter once in such a refusal.
  `convolver-equalizer-apo` refuses an option it does not know, instead of
  making a folder named after it (such as `--output`).
- `convolver-preset`: without `--name`, a preset's name drops only the
  format's ending, as the help says (`Concert.preset.aupreset` is named
  `Concert.preset`, not `Concert`). A filter redirected to `/dev/stdin`, or
  given as `/dev/fd/N`, is read as that file, as rendering already did, so
  the preset keeps the filter file's name (it was `stdin`) and a `.cfg`
  finds its IR files beside it; `--compare` reads such a file too, which on
  macOS was reported as an unknown format. An output that already exists
  is refused at once, before a long filter is read, and writing a
  `.vstpreset` takes less memory.
- Loading a long filter finds its gain figures sooner (about 17 ms instead
  of 50 ms for a 120-second stereo filter on a test machine); the figures
  are the same, apart from a possible change in the last bit on some
  compilers, far too small to change any gain.
- A filter or configuration read from a pipe is written to its temporary
  file as it arrives instead of being held whole in memory first; the size
  limit is the same.
- On macOS, `uninstall.sh` now removes itself, so
  `/Library/Application Support/Convolver` no longer stays behind holding
  only the uninstaller. A folder holding files of your own is still kept.
- `convolver -h` now shows the help, and `convolver-preset --version` gives
  the tool's version. Source builds no longer make `convolver-vst3-preset`,
  the preset tool's older name: use `convolver-preset`.
- Saved projects and presets: every project and preset saved with an
  earlier preview opens, except one whose filter is within 960 samples (all
  impulse responses together: 20 ms of a mono filter at 48 kHz) of the
  largest filter that preview accepted, about 16.8 million samples.
  Convolver now keeps 4 KiB of the 64 MiB a project holds for its settings
  and a preset name, so that later versions can add to it without refusing
  filters this one accepts; such a filter is refused, with the size
  explanation, rather than shortened. Shorten its impulse responses and
  load it again, or keep the earlier preview for that project: saving the
  project after the refusal keeps no filter. The largest filter a plug-in
  accepts is 960 samples shorter.
- For programs built on Convolver's C++ library: the installed package no
  longer includes the plug-ins' host layer, `convolver/host.hpp` and
  `Convolver::host`: `HostProcessor`, `HostFilter`, the host settings and
  gain helpers (`filter_gains()` among them), the saved-state codec, the
  routing and status helpers, all of `host.hpp`. Nor does it include
  `convolver/advice.hpp` (`Advice`), whose parts an exception's `what()`
  gives, or `Convolver::configuration`, whose `.cfg` loader is now inside
  `Convolver::io`. The library is `Convolver::core` and `Convolver::io`.
  The `OutputFormat` shorthands and `Pcm16Quantizer` are gone from
  `io.hpp`. A core-only installation over an earlier full one no longer
  passes for one with `io`. `worst_case_peak()` now refuses a configuration it cannot use,
  as the library's other functions do, and a moved-from `Quantizer` is safe
  to use. `CHANGELOG.md` lists every removed name and what to use instead,
  and `docs/develop/library.md` in the source says how to build against the
  library.
- New guides: "Free impulse responses to try" lists free reverb
  collections, their licences and how Convolver plays each, and the
  Audacity guide gives how long a long filter delays the start of playback.
  The preset guide now says up front that a mono filter plays on each
  channel, so a mono reverb needs no stereo copy, and the command-line
  guide gives `--compare`'s size limit.

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
- <!-- full releases --> The LV2 plug-in (Linux) has no window: choose a filter
  with the host's file chooser for its Filter parameter, where the host
  shows one, or load an LV2 preset made with `convolver-preset` ([plug-in
  presets]({docs}/presets.md)), and set Gain, Mix and Latency with the
  host's own controls. Each channel layout is its own plug-in. It was
  checked automatically, with lilv, lv2lint, pluginval and headless Ardour 9
  sessions, and from a source build in PipeWire 1.6.8 on virtual devices
  (a test in a desktop host, when the release has one, is named above). In
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
- Checked only by automated tests: new since 0.19.21, everything under What
  changed; new in 0.19.21, the changes [its
  notes]({base}/releases/tag/v0.19.21) list; new in 0.18.29, the changes [its
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
  earlier previews open in this one, except one whose filter is within 960
  samples of the largest an earlier preview accepted (see What changed).
