<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-3

## For contributors

- Releases (REL-3): the draft release's notes describe the seventh
  unsigned developer preview, made for jrp's live host testing and the
  independent walkthrough: `PREVIOUS_PREVIEW` is 0.14.0, and `CHANGES` in
  `scripts/release/prepare_release.py` lists in users' words what changed
  since (a filter chosen before the host plays is kept, a configuration
  list loads by itself once the host plays, a filter that does not suit the
  host waits for a start that suits it, Status names a setting not in use,
  the tail after a stop, bypass wording, and the new Ardour and limits
  guides). The known limitations add the host checks not repeated on this
  version, Ardour's VST3 channels, and the features new since 0.14.0 that
  only automated tests have checked
  ([releasing](../docs/develop/releasing.md#publishing-an-unsigned-preview)).

## Decisions: Releases

- **21 November 2026.** A seventh unsigned developer preview, for live
  host testing, cut once the open pull requests of that evening were in
  `main`, PLUG-42's fixes from jrp's review included (jrp: "When the current
  set of PRs is in main, cut a further preview release for live testing",
  and "don't make another preview release without these changes"). The
  host checks not repeated on it stay listed in its notes as not checked.
  The full CI runs for every release and preview release (jrp, the same
  day). See [Releasing](../docs/develop/releasing.md#publishing-an-unsigned-preview).
