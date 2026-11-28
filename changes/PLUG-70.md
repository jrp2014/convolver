<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-70

## Changed

- Plug-ins (PLUG-70): saved state keeps 4 KiB, up from 256 bytes, of its
  64 MiB limit for the plug-in's settings and a preset name, so that later
  versions can save more beside the filter without refusing a filter this
  one accepts. The largest filter a plug-in accepts is 960 samples shorter:
  just under 16,776,192 samples counting every impulse response together.
  A project or preset saved by an earlier version (up to 0.19) whose filter is within
  960 samples of the old limit no longer opens: it is refused with the
  "too large to save" explanation, which now says so, and the filter
  already playing is kept; it is never shortened. Every other project and
  preset from earlier previews opens as before
  ([size limit](../docs/use/plugins.md#when-a-filter-does-not-fit)).

## For contributors

- PLUG-70: `host_settings_room_bytes` is 4096; `host_state_header_bytes`
  follows. `native.host` `saved_state_limit` adds the largest accepted filter saved
  with the longest preset name, the settings and a Gain mode, reopened and
  loaded again, and a state a preview could save (filter 960 samples over
  1.0's limit) that decodes but is refused on load with the explanation,
  keeping the playing filter.

## Decisions: Identities and file formats

- **27 November 2026.** Before 1.0, the room saved state keeps for metadata
  grows from 256 to 4096 bytes (3,840 bytes more, 960 samples), with one
  rule: never accept a filter that cannot later be saved. jrp: "If that
  excludes a narrow band of preview sessions, say so explicitly; don't
  silently truncate them." The release notes say which saved states 1.0
  can still open. See the
  [plug-in contract](../docs/develop/plugin-contract.md#host-state).

## Done

- [x] **PLUG-70 — Keep 4 KiB of saved state for metadata before 1.0.**
  27 November 2026, PR #371, jrp's decision on the pre-1.0 review of 0.19.21
  ([size limit](../docs/use/plugins.md#when-a-filter-does-not-fit)).
