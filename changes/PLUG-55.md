<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-55

## Fixed

- Plug-ins (PLUG-55): numbers in the window, Status, the report, the log and
  messages always use a decimal point, whatever number format the host
  runs in. In a host whose process follows a German or French number
  format the Gain box read `-6,0` instead of `-6`, `0` became `-0,0`, the
  LV2 plug-in's refusal in the host's log could give the rate with a comma,
  and on macOS the
  dimmed Gain slider in Estimated or Clip-safe sat at the wrong place
  (`-12,5` was read as -12). See
  [the window](../docs/use/plugins.md#convolvers-window).

## For contributors

- Host text (PLUG-55): `portable/decimal_text.hpp` (`fixed_text()`,
  `general_text()`, `decimal_value()`) writes and reads numbers in the
  classic locale, in place of `snprintf` and `strtod` in `setting_text()`,
  the report, the window model, Status, sample positions and the LV2
  instance refusal; a row in [invariants to preserve](../docs/develop/architecture.md#invariants-to-preserve)
  now says so.
  `HostReport::gain_played_db` and `WindowContent::gain_played_db` carry
  the gain Estimated or Clip-safe plays as a number, so neither window
  parses the box's text (report encoding 3). Tests: `native.host_settings`,
  `native.host_report` and `native.window_model` run their text checks
  again in a locale with a decimal comma when the machine has one
  (`tests/native/comma_locale.hpp`).

## Done

- [x] **PLUG-55 — Numbers that do not follow the host's locale.** jrp's
  review of `7bd024f`, 26 November 2026, PR #309
  ([the window](../docs/use/plugins.md#convolvers-window)).
