<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-53

## Changed

- Plug-ins (PLUG-53): the window heads a named preset with the preset's own
  name (`Stereo echo`), whether it came from the host's preset menu or from
  Choose Filter, and names the file it was made from in the file details
  (`Filter file: impulse.wav, from …`). Saving and reopening the project
  keeps the name, in every format. Choosing an IR file or `.cfg` directly
  shows that file's name, as before, and drops the preset's name. An older
  or unnamed preset still shows its filter file's name. See
  [the window](../docs/use/plugins.md#convolvers-window).
- `convolver-preset` (PLUG-53): every preset it makes carries its own name,
  in all three formats: `--name`, or the first AU or LV2 output's file name,
  or with only `.vstpreset` outputs the first one's. Converting a preset
  gives it the new name. An `.aupreset` also keeps the filter file's name.
  See [plug-in presets](../docs/use/presets.md#make-a-preset).
- **Opening in an earlier preview (PLUG-53):** a project or preset that
  holds a preset's own name cannot be opened by Convolver 0.18.29 or any
  earlier preview, which refuses it as saved by a newer version. That
  includes every preset `convolver-preset` now makes and any project whose
  filter came from such a preset. Projects whose filter was chosen straight
  from a file still open there, and everything earlier previews saved opens
  in this one.

## For contributors

- Host state (PLUG-53): version 5 adds `HostState::preset_name` after the
  flags (its byte length, then the UTF-8 bytes packed into zero-padded
  words), with the settings behind it when flag 8 is set. It is written only
  for a `storable_preset_name()` (at most `max_preset_name_bytes`, 200
  bytes); `HostFilter::load` sets `HostRequest::preset` from it, and the
  heading and the "Filter file" line come from `heading_name()` and
  `filter_file_name()`. The Audio Unit keeps the filter file's name under
  the new `ClassInfo` key `convolver-filter-name`. See
  [host state](../docs/develop/plugin-contract.md#host-state) and
  [preset files](../docs/develop/plugin-contract.md#preset-files).
- Tests (PLUG-53): `native.state` (version-5 fixtures, round trips and
  refusals), `native.host_report` (the heading, the file line and the kept
  filter's words with a preset name), `vst3.adapter` and `au.adapter`
  (`preset_name_heading`: from the menu and from Choose Filter, reopened,
  and cleared by a WAV), `vst3.preset` and `plugins.presets` (the tool's
  names).

## Done

- [x] **PLUG-53 — The preset's own name as the window's heading.** jrp's
  choice of 26 November 2026, from the window's design review, with one
  shared preset name in the host state. 26 November 2026, PR #304
  ([the window](../docs/use/plugins.md#convolvers-window)).

## Decisions: Identities and file formats

- **26 November 2026.** A named preset heads the window with its own name,
  and the filter file's name moves to the file details, with three checks:
  a named preset shows its name from the host or Choose Filter and keeps it
  through saving and reopening; an older or unnamed preset shows its filter
  file's name; a directly chosen IR or `.cfg` shows its own name and clears
  the preset's. The name is one shared, optional `preset_name` field in the
  host state for VST3, the Audio Unit and LV2 (host state version 5), not a
  separate field in each format, so bundled and user-made presets behave
  the same. jrp accepted that previews up to 0.18.29 cannot open projects
  and presets saved with a name, to be said plainly in the release notes,
  and dropped reopening new projects in an older preview as a check. See
  [host state](../docs/develop/plugin-contract.md#host-state).
