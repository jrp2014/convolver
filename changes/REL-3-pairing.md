<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-3

## For contributors

- Releases (REL-3): the preview notes no longer say the VST3 halves agree
  "whatever order the host delivers its calls in"; they name what the
  tests cover (host presets, window presets and later Latency, Gain mode,
  Gain and Mix changes, in every order the tests try), from jrp's review
  of 25 November 2026
  ([publishing an unsigned preview](../docs/develop/releasing.md#publishing-an-unsigned-preview)).

## Decisions: Releases

- **25 November 2026.** The ninth preview is not cut from v0.18.20: jrp's
  review of `main` at `33971caf` found that a delayed pairing message can
  reconnect the VST3 window to another instance's processor, so the
  preview waits for that fix and is cut from the tag after it, with its
  own load-budget measurement once PERF-21 is a release step. See
  [Releasing](../docs/develop/releasing.md#publishing-an-unsigned-preview).
