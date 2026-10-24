<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Other programs

[Using Convolver](README.md) · [Configuration files](configuration.md) ·
[Inspection options](command-line.md#inspecting-and-validating-configurations)

For anyone sharing Convolver configurations with other programs, moving them
between computers, or bringing them from the old Windows Convolver.

**In short:** Convolver reads the `.cfg` format introduced in version 3.0 of
the old Windows Convolver, plus a few extensions such as IRs of different
lengths. Roon, JRiver, Audiolense and others read variants of the format, but
none has been tested with Convolver's examples, so treat interchange as
unproven. Before copying a setup to another computer, run
`convolver --validate room.cfg --profile portable-paths`.

Contents: [check a configuration](#check-a-configuration) ·
[check that it will travel](#check-that-a-configuration-will-travel) ·
[what Convolver accepts](#what-convolver-accepts) ·
[what other programs read](#what-other-programs-read) ·
[configurations from the old Windows Convolver](#configurations-from-the-old-windows-convolver)

## Check a configuration

```sh
convolver --validate room.cfg                            # will it render here?
convolver --inspect filters.txt --sample-rate 48000 --input-channels 2
convolver --validate filters.txt --all-configurations    # every list entry
convolver --validate room.cfg --profile portable-paths   # will it travel?
```

[Inspection](command-line.md#inspecting-and-validating-configurations)
describes the report and its JSON form.

## Check that a configuration will travel

A profile changes what is reported, never how a configuration is selected,
resolved or rendered. No profile rewrites paths.

| Profile | Reports |
| --- | --- |
| `convolver` (default) | Errors that stop this machine from rendering: grammar, routes, delays, missing or mismatched IR files, limits. |
| `portable-paths` | The same errors, plus warnings for references that work here but will not travel. |

`portable-paths` warnings, each with file, line and column:

| Code | Meaning | What to do |
| --- | --- | --- |
| `absolute-reference` | A native absolute path (for example `/home/me/irs/a.wav` or `C:\irs\a.wav` on Windows). It only resolves on this machine. | Give the path relative to the configuration. |
| `outside-tree` | A relative path (with `..`) leaving the folder of the inspected file. Copying that folder alone will not carry the IR. | Move the IR into the folder, or ship the parent folder. |
| `case-mismatch` | The spelling differs in letter case from the name stored on disk. It loads on case-insensitive filesystems (usual on macOS and Windows) and fails on case-sensitive ones (usual on Linux). | Match the stored spelling. |
| `windows-invalid-name` | A name containing `< > " \| ? *`, which Windows cannot store. | Rename the file. |
| `raw-filter` | A `.pcm` or `.raw` filter. Convolver reads it as mono `f32le` (or the `--raw-filter-format` type) at the configuration's rate ([filter files](configuration.md#filter-files)); other programs expect sound files. | Convert it to WAV for other readers. |

Separator style is not a warning: `/`, `\` and mixed separators in relative
references load on every platform. Windows drive-letter, UNC and `\`-rooted
references are already errors on macOS and Linux (`nonportable-path`); the
message names the line and, when a file matching the reference's trailing part
exists beside the configuration, suggests that relative path. The file is never
loaded under a guessed name.

A clean `portable-paths` result is evidence about paths only. It is not a
certification for any particular player, plug-in host or operating system.

## What Convolver accepts

"yes" means Convolver loads it, and its tests render it and compare the result
with an independent calculation. The other columns record only what each
program documents; "not tested" means nobody has tried Convolver's test
configurations in that program.

| Feature | Convolver | Roon | Audiolense / JRiver | Other readers |
| --- | --- | --- | --- | --- |
| Sound file as the filter, diagonal channels | yes | not tested | not tested | not tested |
| Header, delay lines, four-line paths (3.0 and later) | yes | documented | documented | not tested |
| Format before 3.0 (no delay lines) | no, with a diagnostic | unknown | unknown | unknown |
| `-0` inversion, fractional weights, repeated routes | yes | not tested | not tested | not tested |
| Cross-channel matrices, summed outputs | yes | not tested | not tested | not tested |
| Selected IR channel | yes | not tested | not tested | not tested |
| Delays, truncated to frames | yes | not tested | not tested | not tested |
| Unequal IR lengths (the 4.4 specification required equal lengths) | yes, extension | unknown | unknown | unknown |
| Hex channel mask | yes, parsed and reported; written as WAV speaker labels only with `--speaker-map config` | not tested | not tested | not tested |
| `//` comments and blank lines | yes | not tested | not tested | not tested |
| UTF-8 BOM, CRLF line endings | yes | not tested | not tested | not tested |
| Relative paths beside the configuration | yes | documented (required) | not tested | not tested |
| `\` and mixed separators in relative paths | yes | not tested | not tested | not tested |
| UTF-8 names with spaces | yes | not tested | not tested | not tested |
| Native absolute paths | yes, warned by `portable-paths` | documented as unsupported | not tested | not tested |
| Windows drive/UNC paths on macOS/Linux | no, with a diagnostic | n/a | n/a | n/a |
| Lists: first entry matching rate and channels (optionally output channels) | yes | not tested | not tested | not tested |
| Lists naming lists | yes | documented as unsupported | not tested | not tested |
| Missing IR in an unselected list entry | yes | unknown | unknown | unknown |

## What other programs read

The other programs' columns come from their own documentation, recorded in
[downstream-evidence.json](../review-2026-10/downstream-evidence.json):

- **Roon**: its convolution help page states Convolver-format support, requires
  relative paths and ZIP packaging with the WAV files, and excludes configurations
  that list other configurations.
- **Audiolense / JRiver**: the Audiolense author credits the format and describes
  persuading JRiver to implement it; JRiver reads a close variant. Neither the
  variant's differences nor the supported versions are documented here.
- **Hang Loose Convolver** links the original specification; **CamillaGUI**
  contains a source-confirmed importer. Neither has been tried with Convolver's
  test configurations.

If you have checked a configuration in one of these programs, please report
what worked, with the program's version.

## Configurations from the old Windows Convolver

The 40 sample configurations and lists in `historical/configs/` (including
`filters/`) came with the Windows releases. None loads unchanged: most predate
the 3.0 format (no delay lines, or an older header) and the rest name absolute
`C:\` or `D:\` paths on the original developer's computer.
`convolver --validate FILE --syntax-only` reports which, and on which line.

To bring one of your own across:

1. Add an input-delay line and an output-delay line after the header, with one
   `0` per channel, if they are missing.
2. Copy the IR files beside the `.cfg` and replace drive paths such as
   `C:\filters\room.wav` with relative ones such as `room.wav`.
3. Run `convolver --validate room.cfg --profile portable-paths` and fix what it
   reports.
