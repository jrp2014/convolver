<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-58

## Changed

- VST3 (PLUG-58): restoring a project or preset no longer decodes the
  filter a second time in the controller, which keeps only the settings
  and bypass. With a large filter the controller had copied and decoded up
  to 64 MiB of it, on the UI thread and, during an offline render, on the
  render's thread, only to discard it. Nothing heard changes.

## For contributors

- State (PLUG-58, jrp's review of `7bd024f`, §2.2):
  `decode_host_state_header()` reads what a state says before its filter
  (bypass, failed load, settings, preset name, whether a filter follows)
  from its first `host_state_header_bytes`, with the same checks as
  `decode_host_state()`, which now uses it. The VST3 controller's
  `setComponentState()` reads states with `read_state_header()`, which
  reads the filter past a block at a time, so the stream ends where it did
  and a state cut short is still refused. One difference: a state whose
  start is sound but whose filter is damaged now gives an unpaired
  controller its settings, where it was refused, and one restored off the
  UI thread while paired now keeps its settings pending, where it was
  ignored, so a controller whose processor goes before the next call on
  the UI thread shows them. The processor still refuses the state.
  Tests: `native.state` checks that the header agrees with the full decode
  for every version, from the whole state and from its start, that for
  states without a filter both accept the same bytes under every
  single-byte change and truncation, and that a start longer than the
  state or a size past the limit is refused; `vst3.adapter`'s
  `component_state_header_only` restores a 100,000-tap state on and off
  the UI thread and checks the settings shown, the stream position and the
  refusal of a state one byte short, and that a state whose filter is
  damaged gives an unpaired controller its settings while the processor
  refuses it.

## Done

- [x] **PLUG-58 — The VST3 controller reads only a restored state's
  header.** jrp's review of `7bd024f`, 26 November 2026, PR #331
