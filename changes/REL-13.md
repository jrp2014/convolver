<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-13

## For contributors

- Releases (REL-13): the words of a release's notes that change from one
  release to the next (the preview published before, what changed and the
  known limitations) are now in
  [`packaging/release-notes.md`](../packaging/release-notes.md) instead of
  in `prepare_release.py`, so a release-notes pull request edits a page
  rather than the script the production gate runs. A known limitation can
  be marked for previews only or for full releases only: a full release's
  notes no longer say that the Windows window was not tried in a Windows
  host, which its host evidence contradicts; the LV2 sentence stays, since
  a Linux host test does not say which format it used. The page refuses a
  section that appears twice and any comment or placeholder it does not
  define. The preview notes are unchanged, byte for byte.
  `prepare_release_tests.py` checks the page and its parser, pre-commit
  runs it whenever the page, the script or its tests change, and the draft
  release workflow's pull-request dry run runs when the page changes
  (jrp's review of `7bd024f`, §4.2, and the review comment on PR #314). See
  [publishing an unsigned preview](../docs/develop/releasing.md#publishing-an-unsigned-preview).

## Done

- [x] **REL-13 — Release-notes text outside the release script.** jrp's
  review of `7bd024f`, 27 November 2026, PR #336
  ([releasing](../docs/develop/releasing.md#publishing-an-unsigned-preview)).
