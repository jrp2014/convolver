# Configuration compatibility

What the portable Convolver accepts in a configuration, what has been rendered
and checked, and what is known about other programs that read the same format.
Each claim points at a corpus case in [tests/compatibility](../tests/compatibility/README.md)
or at recorded evidence. Unknown stays unknown: no program below has been tested
with these fixtures.

## Checking a configuration

```sh
convolver --validate room.cfg                            # will it render here?
convolver --inspect filters.txt --sample-rate 48000 --input-channels 2
convolver --validate filters.txt --all-configurations    # every list entry
convolver --validate room.cfg --profile portable-paths   # will it travel?
```

See [Inspecting and validating configurations](portable-core.md#inspecting-and-validating-configurations)
for the report contents and JSON schema.

## Profiles

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

Separator style is not a warning: `/`, `\` and mixed separators in relative
references load on every platform. Windows drive-letter, UNC and `\`-rooted
references are already errors on macOS and Linux (`nonportable-path`); the
message names the line and, when a file matching the reference's trailing part
exists beside the configuration, suggests that relative path. The file is never
loaded under a guessed name.

A clean `portable-paths` result is evidence about paths only. It is not a
certification for any particular player, plug-in host or operating system.

## Compatibility matrix

"Accepted" means the portable parser loads it; "Rendered" means the corpus
renders it at block sizes 1, 3, 17 and 256 in both latency modes and matches an
independent direct convolution. The consumer columns record only documented
statements; "not tested" means nobody has run these fixtures in that program.

| Feature | Accepted | Rendered (corpus case) | Roon | Audiolense / JRiver | Other readers |
| --- | --- | --- | --- | --- | --- |
| Sound file as the filter, diagonal channels | yes | `wav-identity`, `wav-stereo-diagonal` | not tested | not tested | not tested |
| Header, delay lines, four-line paths (3.0 and later) | yes | `cfg-identity` | documented | documented | not tested |
| Format before 3.0 (no delay lines) | no, with a diagnostic | `pre-3.0-format` | unknown | unknown | unknown |
| `-0` inversion, fractional weights, repeated routes | yes | `signed-zero-inversion`, `fractional-repeated-routes` | not tested | not tested | not tested |
| Cross-channel matrices, summed outputs | yes | `cross-channel-mixing` | not tested | not tested | not tested |
| Selected IR channel | yes | `ir-channel-select` | not tested | not tested | not tested |
| Delays, truncated to frames | yes | `integer-frame-delays`, `truncated-delays`, `delay-float-precision` | not tested | not tested | not tested |
| Unequal IR lengths (the 4.4 specification required equal lengths) | yes, extension | `unequal-ir-lengths` | unknown | unknown | unknown |
| Hex channel mask | yes, parsed and reported; written as WAV speaker labels only with `--speaker-map config` | `hex-channel-mask` | not tested | not tested | not tested |
| `//` comments and blank lines | yes | `comments-blank-lines` | not tested | not tested | not tested |
| UTF-8 BOM, CRLF line endings | yes | `bom-crlf` | not tested | not tested | not tested |
| Relative paths beside the configuration | yes | `relative-posix-parent`, `cwd-relative-filter` | documented (required) | not tested | not tested |
| `\` and mixed separators in relative paths | yes | `relative-windows-separators`, `relative-mixed-separators` | not tested | not tested | not tested |
| UTF-8 names with spaces | yes | `unicode-and-spaces` | not tested | not tested | not tested |
| Native absolute paths | yes, warned by `portable-paths` | `absolute-native-path` | documented as unsupported | not tested | not tested |
| Windows drive/UNC paths on macOS/Linux | no, with a diagnostic | `drive-letter-path`, `unc-path` | n/a | n/a | n/a |
| Lists: first entry matching rate and channels (optionally output channels) | yes | `list-first-match`, `list-of-sound-files` | not tested | not tested | not tested |
| Lists naming lists | yes | `nested-list-windows-separators` | documented as unsupported | not tested | not tested |
| Missing IR in an unselected list entry | yes | `list-missing-unselected-asset` | unknown | unknown | unknown |

Sources for the consumer columns are recorded in
[downstream-evidence.json](review-2026-10/downstream-evidence.json):

- **Roon**: its convolution help page states Convolver-format support, requires
  relative paths and ZIP packaging with the WAV files, and excludes configurations
  that list other configurations.
- **Audiolense / JRiver**: the Audiolense author credits the format and describes
  persuading JRiver to implement it; JRiver reads a close variant. Neither the
  variant's differences nor the supported versions are documented here.
- **Hang Loose Convolver** links the original specification; **CamillaGUI**
  contains a source-confirmed importer. Neither has been run with these fixtures.

Before adding a named profile for one of these programs, record its
specification or version, add fixtures for its documented differences, and test
them in that program. Use the `consumer-verified` corpus label only for cases
that were.

## Bundled historical samples

The 40 sample configurations and lists in `convolver/configs/` (including `filters/`) came with the Windows releases. None
loads unchanged with the portable reader: most predate the 3.0 format (no delay
lines, or an older header) and the rest name absolute `C:\` or `D:\` paths on the original
developer's machine. `--validate FILE --syntax-only` reports which, and on which
line. They remain unchanged as historical material; `native.inspection_cli` checks
that each is diagnosed rather than crashing.
