<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-38

## Changed

- Command line (PERF-38): a filter or configuration read from a pipe is
  written to its private temporary file as it arrives, instead of being
  held whole in memory first. The size limit and each message's wording
  are unchanged; when a pipe is too large and the temporary folder is also
  missing or full, the folder is now the one reported.

## For contributors

- `PipedFilter::copy()` in `portable/main.cpp` creates the private folder
  first, then copies the pipe in 64 KiB reads with the same limit, removing
  the folder before reporting a read failure, a filter over the limit or a
  write failure. `tests/native/cli_tests.py` covers a missing temporary
  folder and, under a file-size limit, a copy that fails part-way; the
  read-failure and over-limit paths were checked by hand (a socket given as
  the filter, and a pipe of 513 MiB and one byte).

## Done

- [x] **PERF-38 — Stream a piped filter to its temporary file.**
  28 November 2026, from the release audit of 0.22.0.
