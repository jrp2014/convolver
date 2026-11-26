<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-1

## Changed

- Release (REL-1): the release files no longer say "preview" in their names:
  each platform's ZIP is `Convolver-X.Y.Z-<platform>.zip`, from the next
  candidate on, and the installers drop it too. The `-unsigned` in the
  Windows and macOS installer names stays until those packages are signed.
  A preview is still a GitHub pre-release with "preview" in its title.

## Decisions: Releases

- **26 November 2026.** No release file name says "preview": a preview and
  the full release name their files the same way, `Convolver-X.Y.Z-<platform>.zip`,
  and only the release's title and pre-release mark tell a preview apart
  (jrp chose "Drop it from all names now"). See
  [production gates](../docs/develop/releasing.md#production-gates).
