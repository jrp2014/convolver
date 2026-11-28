<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-38

## Changed

- Command line (PERF-38): a filter or configuration read from a pipe is
  written to its private temporary file as it arrives, instead of being
  held whole in memory first. The size limit and the messages are
  unchanged.

## For contributors

- `PipedFilter::copy()` in `portable/main.cpp` creates the private folder
  first, then copies the pipe in 64 KiB reads with the same limit, removing
  the folder before reporting a read failure, a filter over the limit or a
  write failure. The piped-filter checks in `tests/native/cli_tests.py`
  cover each path.

## Done

- [x] **PERF-38 — Stream a piped filter to its temporary file.**
  28 November 2026, from the release audit of 0.22.0.
