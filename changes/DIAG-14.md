<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DIAG-14

## Changed

- Command line and plug-ins (DIAG-14): when a filter is refused only for its
  sample rate, an IR file's explanation no longer describes how channels are
  routed (which, for a mono IR file and mono audio, read "it needs audio with
  1 channel; a one-channel file filters every channel"); it says the file
  needs audio at its rate
  ([troubleshooting](../docs/use/troubleshooting.md#filters-and-configurations)).
- Command line (DIAG-14): `convolver` warns when the output's name says one
  file type and `--output-format` (or the default, `wav-float`) writes
  another, such as a WAV file named `out.flac`; the file is still written
  as before ([output formats](../docs/use/command-line.md#output-formats)).
- Command line (DIAG-14): `convolver --validate` names the file once in a
  refusal for the requested rate or channels, not as "ir.wav: ir.wav cannot
  be used", and the note after a failure part way through standard output
  starts a sentence of its own.
- Equalizer APO export (DIAG-14): `convolver-equalizer-apo` refuses an
  argument that starts with `-` other than `--help`, `-h` and `--version`
  alone, instead of creating a folder named after it (such as `--output`);
  write `./-name` for a name that starts with `-`
  ([Equalizer APO](../docs/use/equalizer-apo.md)).

## For contributors

- Tests (DIAG-14): `native.cli` checks each message and that RF64 named
  `.wav`, W64 named `.w64` and a name without an extension give no warning;
  `export.equalizer_apo` checks the refused options. From Claude's audit of
  0.22.0.

## Done

- [x] **DIAG-14 — Messages and option checks from the audit of 0.22.0.**
  28 November 2026, from Claude's audit of 0.22.0
  ([output formats](../docs/use/command-line.md#output-formats)).
