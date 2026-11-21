<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-42

## Fixed

- Plug-ins (PLUG-42, jrp's decision of 21 November 2026): in the VST3 and
  Audio Unit plug-ins, a filter that does not suit the host's sample rate or
  channels, as a preset, an IR file or a `.cfg`, is refused only while
  another filter that suits the host is playing. With nothing playing it is
  kept and saved with the project, plays silence with the reason in Status,
  and plays when the host next starts the plug-in at its rate and with its
  channels; the log says it waits for audio settings that suit it. Until
  then Mix's dry share and bypass pass the input where the host's and the
  filter's counts are all equal, as for any filter kept that way, where a
  refused first filter played silence. A filter or the input that played
  before it mutes within 1.3 ms; a filter kept that way before is replaced
  without a gap in the dry share or bypass where the new one passes them
  too. Before, it was refused and
  the project saved the failure: in Ardour 8.4, a filter chosen just
  before Ardour restarted the plug-in with the outputs set in Pin
  Connections was refused and the export was silent. A configuration list with no entry for the host is still
  refused. A filter refused while another plays stays refused, and the
  refusal now says the filter Convolver has stays selected when the host
  restarts the plug-in, even with settings that suit the refused one. The
  LV2 plug-in, whose settings never change, refuses as before.

## Done

- [x] **PLUG-42 — Keep a filter refused only for the host's channels or
  rate.** 21 November 2026, PR #258, jrp's decision of the same day (found
  in Ardour 8.4 by HOST-10): VST3 and the Audio Unit refuse a filter that
  does not suit the host only while one that suits it plays; otherwise
  they keep it, save it as the filter and play it at the next start that
  suits it ([contract](../docs/develop/plugin-contract.md#loading-and-failure)).

## Decisions: Plug-in behaviour

- **21 November 2026.** PLUG-42, from HOST-10's Ardour 8.4 runs (a preset
  loaded just before Ardour restarted the VST3 plug-in with the outputs set
  in Pin Connections was refused, never tried at the outputs that suited
  it, and saved as a failure). Asked whether a filter refused only for the
  channels should be tried again when the host restarts the plug-in with
  channels that fit, jrp: "Yes. That's what the user would expect." Then,
  choosing "fix the Ardour case now and record the other case in TODO":
  in VST3 and the Audio Unit, when no working filter plays, a filter that
  does not suit the host's rate or channels is kept, saved as the filter,
  and the user is told it waits for compatible audio settings; it plays at
  the next start with a matching rate and channel layout. When another
  filter already plays, it stays and the new choice is refused, so "a user
  who sees that refusal should be able to trust that restarting playback
  won't unexpectedly switch filters". The latest choice is the one kept,
  and the rate counts as the channels do. Holding a refused replacement
  beside the playing filter is revisited only if a user asks (PLUG-43).
  LV2, whose configuration never changes, refuses as before. See
  [Contract: loading and
  failure](../docs/develop/plugin-contract.md#loading-and-failure),
  [plug-ins](../docs/use/plugins.md#when-a-filter-does-not-fit).
