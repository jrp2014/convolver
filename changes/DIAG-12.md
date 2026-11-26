<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DIAG-12

## Changed

- Plug-ins (DIAG-12): the Convolver log's start line for an instance with no
  filter reads `No filter. Audio passes unchanged.` (was `No filter, so
  audio passes unchanged.`): "No filter." and then the words the window
  shows under its None heading; likewise for a level set by Gain and Mix
  and for inputs and outputs that differ. The window's text is
  unchanged. See [status and log](../docs/use/plugins.md#status-and-the-convolver-log).

## For contributors

- Host reports (DIAG-12): the window no longer builds its no-filter message
  by recognising the log's sentence, removing its "No filter" lead and
  capitalising the rest: `no_filter_words()` in `portable/host_log.cpp`
  says what plays once, and `playing_words()` puts "No filter." before it
  for the log. `duration()` in `portable/host_report.cpp` formats the bare
  time itself ("5.3 ms"); `seconds()`, which added brackets for
  `duration()` to remove, is gone. `native.host_report` `log_agrees` pins
  the four no-filter messages in the window and the log.

## Done

- [x] **DIAG-12 — Window text not taken apart from the log's sentences.**
  jrp's review of `547e2dd8`, 26 November 2026, PR #318
  ([status and log](../docs/develop/plugin-contract.md#status-and-log)).
