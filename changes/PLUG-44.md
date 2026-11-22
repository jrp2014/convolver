<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-44

## Changed

- Plug-ins (PLUG-44): a filter kept because it does not suit the host's
  sample rate or channels now shows the Status **Waiting**, not Load failed,
  with what it waits for: `Silent, waiting for 44100 Hz: the host runs at
  48000 Hz.` (or `Only the dry signal plays, waiting for …` where Mix lets
  the input through). Nothing was refused, and it plays once the host starts
  the plug-in with settings that suit it. Load failed now always means a
  request was refused or a filter could not start; after a refusal it stays
  until a load is accepted, as before. The window shows Waiting in plain
  grey rather than red. LV2's `status` port keeps Load failed for it. See
  [status](../docs/use/plugins.md#status-and-the-convolver-log).
- Plug-ins (PLUG-44): the window says each fact once. When the host's rate
  and channels suit the filter, the Audio section shows one line, `Host
  48000 Hz, 2 in → 2 out ✓ suits the filter`, instead of two; before the
  host has sent its settings it says `not known yet` once; Routing shows
  only with a filter; Ring-out shows only when it
  says more than the filter's Length (delays, a Latency, another rate);
  channels read `2 in → 2 out`
  everywhere. The reminder that a file can be dropped on the window is
  gone from under the name (the Fix and Choose…'s tip still say it). When
  a file is refused, the message now explains that file first and says
  once what becomes of the filter in the heading, by name: `room.cfg keeps
  playing.` while the host plays it, `room.cfg is still selected.` after
  Stop, and, for a kept filter that does not suit the host, `room.cfg is
  still selected and will play at 48000 Hz.` instead of telling you to
  change the project's rate (jrp's review). The note under the name says
  only `broken.wav was refused.`, and now also shows when a file is
  refused while a kept filter waits (before, only the log and the host's
  beep said so); the message no longer repeats the Latency row's "not in use yet"; the
  title gives the release number without the build's source revision,
  which the Software line keeps. Loudest peak is renamed **Worst-case
  peak**, so that it cannot be confused with the level meters planned
  under PLUG-10. See [Convolver's
  window](../docs/use/plugins.md#convolvers-window).
- Plug-ins (PLUG-44): for hosts that show only their own controls, Status
  ends with the gain that Estimate or No clipping plays (`Estimate plays
  -7.3 dB.`); Latency values read `0 frames (none)` for 0 in every format
  and, in VST3 and the Audio Unit once the host has started the plug-in,
  `256 frames (5.3 ms)`, and a VST3 host that types a
  Latency must give one of the listed frame counts; the VST3 parameters
  have short names (Gain, Mode, Mix, Lat, Bypass, Status); and LV2's
  latency output is named Reported latency, so hosts no longer show two
  controls called Latency. See [settings](../docs/use/plugins.md#settings).

## For contributors

- Host layer (PLUG-44): `HostFilter::waits_for_settings()` (Kept, unsuited
  with no refused request) gives the Waiting value; `latency_value_text()`
  in `host_report.hpp` names a Latency for every format. Tests in
  `native.host`, `native.host_report`, `native.window_model`,
  `vst3.adapter`, `au.adapter` and the macOS window tests follow the new
  text ([contract: status and log](../docs/develop/plugin-contract.md#status-and-log)).

## Done

- [x] **PLUG-44 — Say each fact once in the window and host views.** 21
  November 2026, PR #267, jrp's choice of items 1 to 12 from the
  interface review of the same day
  ([window](../docs/use/plugins.md#convolvers-window)).

## Decisions: Plug-in behaviour

- **21 November 2026.** From the interface review (PLUG-44), jrp chose
  "Fixes now, presets later": a filter kept until the host's settings
  suit it shows Waiting rather than Load failed (answering PLUG-42's open
  question); the window states each fact once, which merges the host's
  rate and channels into one Host line when they suit the filter, partly
  reversing the 6–7 November choice to show the host's values beside the
  filter's even when equal (they still show separately, and marked, when
  they differ); Loudest peak becomes Worst-case peak; generic host views
  get the played gain, Latency times and VST3 short names; built-in
  example presets wait until after the preview (PLUG-45). See
  [status](../docs/use/plugins.md#status-and-the-convolver-log),
  [window](../docs/use/plugins.md#convolvers-window).
