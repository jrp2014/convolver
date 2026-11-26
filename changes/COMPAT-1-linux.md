<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# COMPAT-1

## For contributors

- Upgrade tests (COMPAT-1): the published 0.18.29's files are kept in
  `tests/upgrade/0.18.29/`: its example presets and first-use examples, the
  audio its own CLI and VST3 plug-in rendered from them, and the state its
  LV2 plug-in saved, so `upgrade.cli`, `vst3.upgrade` and `lv2.upgrade`
  check every later build against them. Its renders and LV2 state match
  0.14.0's byte for byte, except the echo presets, whose filter changed. See
  [files from released builds](../tests/upgrade/README.md).
