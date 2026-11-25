<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DIAG-11

## Changed

- Plug-ins (DIAG-11): the VST3 plug-in's log line for a channel layout it
  cannot take now reads as an offer it declined, not a failure: "The host
  offered 64 input and 64 output channels; Convolver takes 1 to 8 of each,
  so it declined.", with a note that hosts often offer several layouts and
  that the next "Started at" line gives the one in use. REAPER offers every
  instance 64 channels each way first, so every REAPER session logged a
  "Refused" line that looked like a fault (jrp's soak of 25 November 2026).
  On macOS the line goes to the unified log as information, no longer as an
  error. The command-line advice still appears only for a layout with more
  than 8 channels. See
  [status and the Convolver log](../docs/use/plugins.md#status-and-the-convolver-log).

## Done

- [x] **DIAG-11 — Word a declined channel layout as an offer.** 25 November
  2026 (jrp asked for it after the HOST-12 soak's log).
