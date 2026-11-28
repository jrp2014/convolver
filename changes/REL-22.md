<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-22

## Fixed

- macOS installer (REL-22): `uninstall.sh` now removes itself, so
  `/Library/Application Support/Convolver` no longer stays behind, holding
  only the uninstaller, once everything else is removed. A folder holding
  files of your own is still kept ([install](../docs/use/install.md)).

## For contributors

- Tests (REL-22): `installer_tests` runs the generated script from its
  installed place and requires the folder gone, and `macos_installer.py`
  requires the same after a real install and uninstall. From the second
  audit of 0.22.0 (§2.1).

## Done

- [x] **REL-22 — The macOS uninstaller removes itself and its folder.**
  28 November 2026, from the second audit of 0.22.0
  ([install](../docs/use/install.md)).
