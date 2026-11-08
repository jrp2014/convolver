<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# File-loading review, 7 November 2026

> **Dated record.** jrp's review of filter files, presets and their
> locations, written alongside the
> [Audacity session](2026-11-07-host1-audacity.md) and posted in the project
> thread on 8 November 2026 at 00:00 UTC; copied here verbatim below the
> line, without the SPDX line it began with. Its `results.md` is that
> session's report. The file and line pointers name the source at `ac329b5`.
> What it led to: PLUG-25 to PLUG-29 and DOC-10 in [TODO](../../../TODO.md),
> and the guide fixes listed under DOC-10.

---

# Filter files, presets and locations: review, 7 November 2026

The useful simplification is one dependable way to load a filter from a file the
user already has. File extensions and host preset directories currently decide
which loading route works, although the result is the same embedded filter.
Keep the existing formats and compatibility rules; avoid adding another format
or a library-management subsystem merely to hide these differences.

This is a review, not an implemented change. Source examined: the local checkout
at ac329b5, the installed 0.10.13 candidate, and the current main versions of the
preset and host-testing guides fetched during this session. The Audacity evidence
is recorded separately in results.md. Nothing in the repository was edited.

## Findings and priorities

### 1. Make the ordinary loading route independent of host preset discovery

Convolver's Choose dialog accepts an arbitrary file but sends it through
`portable/filter_file.cpp`: configurations, lists and IR sound files. The preset
CLI has a separate `.vstpreset` path through `presets::read_vst3_preset()`.
Consequently the self-contained files we distribute cannot be loaded through the
plug-in's own Choose button. Users must learn the host's preset importer and
search paths. Audacity 4's importer uses its text parameter format, and its VST3
factory scan uses a system folder. That is a concrete product friction, not a
reason to install ordinary test assets with administrator privileges.

Recommendation: share a bounded input-reading dispatcher between the preset CLI
and plug-in file loader. Add Convolver `.vstpreset` input to the existing Choose
and drop routes first, reusing the existing decoder and identity/size checks.
Keep configurations, nested lists and their relative IR paths working as today.
Do not add an AU XML/plist or LV2 parser merely for symmetry; use existing native
readers only if a demonstrated need justifies those additions.

One semantic choice needs to be explicit before implementation: does this command
load only the filter, preserving current Gain/Mix/Latency, or restore all settings
from a full preset? The existing converter extracts the filter; a host-saved
preset may contain settings too. Name the command and explain its behaviour
consistently. Do not silently discard settings while claiming full preset import.

Acceptance: load the same self-contained file from an arbitrary user folder in
AU and VST3, with no preset installation, conversion or administrator action;
save/reopen and reproduce the CLI output. Reject a foreign plug-in's preset or
malformed container with an actionable message.

Pointers: `plugins/editor/mac/diagnostic_view.mm:857`,
`portable/filter_file.cpp:55`, `portable/preset_main.cpp:129`,
`portable/presets.cpp:140`.

### 2. Separate reading a filter from checking a provisional host format

The VST3 direct-file path uses `known_layout().value_or(layout())` before reading
and selecting the file. In this run a 48000 Hz .cfg was rejected against 44100 Hz
before the intended 48000 Hz operation. A preset can instead be retained before
activation and checked when the host supplies its actual format. Export rate and
live playback rate also differed in Audacity until its preference was changed.

Recommendation: use the existing host lifecycle rules for both input routes.
Decode a single self-describing IR/configuration/preset without inventing a
confirmed host rate, then validate through the common acceptance gate. A list
really does need rate/channel selection: explicitly defer selection when those
values are not established, rather than silently selecting an arbitrary entry.
Retain the current protections for incompatible replacements during playback.
No automatic resampling or audio-default change is implied.

Suggested UI text before configuration:

> Filter loaded: 48 kHz, stereo. Waiting for audio settings from the host.

This avoids “checked when the host starts it”, which sounds like an instruction
to restart the application. Explain multiple host instances in expanded details
when relevant; do not assume every Audacity version uses the same instance model.
In this run the live instance and export instances have different numbers.

Pointer: `plugins/vst3/processor.cpp:590` and the common host lifecycle/acceptance
code. Test stopped/unconfigured loading, live rejection, changed host rates and
configuration-list selection without duplicating another acceptance state machine.

### 3. Correct and shorten the primary instructions

The current main preset guide still says “The plug-ins have no editor” and
“The LV2 plug-in has no file chooser yet”. Both contradict the current plug-in
guide. This unnecessarily funnels readers into conversion and installation.

The normal instructions should be: choose a filter, confirm its rate/channels,
play, save the project. Put host-specific preset-library setup in a separate
optional section. There should be one owning page for supported inputs and one
location table, linked from the walkthrough rather than repeated.

The host-testing guide's Audacity setup uses `sudo cp`, then suggests removing
the whole `/Library/Audio/Presets/Convolver` directory. Besides violating the
user's home-only preference, that cleanup is too broad if the folder contains
other presets. Replace it with a user-local route when supported. Until then,
state the host limitation and leave the affected check unqualified; do not keep
inventing opaque host-configuration workarounds in the normal walkthrough.

Sources checked on GitHub during this session:

- [Preset guide](https://github.com/jrp2014/convolver/blob/main/docs/use/presets.md)
- [Host-testing guide](https://github.com/jrp2014/convolver/blob/main/docs/develop/host-testing.md)
- [Audacity 4.0.2 preset importer](https://github.com/audacity/audacity/blob/Audacity-4.0.2/src/effects/effects_base/internal/effectpresetsscenario.cpp)
- [Audacity 4.0.2 VST3 preset paths](https://github.com/audacity/audacity/blob/Audacity-4.0.2/au3/libraries/au3-vst3/VST3Utils.cpp)

### 4. Give packaged presets useful names

Every generated journey preset is currently named `filter.vstpreset`, with its
identity encoded only in the parent directory. Audacity's library displays the
filename, forcing an extra rename/copy step. Give new packaged presets names such
as `repeat-48000-stereo.vstpreset`. Keep .cfg and IR files together with relative
references. Preserve released artifacts and compatibility fixtures; update the
package manifest, examples and their tests together rather than renaming files
throughout historical evidence.

Pointer: `scripts/release/generate_examples.py:39`.

### 5. Keep storage responsibilities distinct

| Material | Proposed default on this Mac |
| --- | --- |
| User filters and self-contained presets | Any user-selected folder; no installation required |
| Optional host preset discovery | The host's supported user preset folder; never a prerequisite for Choose |
| Installed plug-in bundles | Existing per-user Audio/Plug-Ins locations |
| Convolver preferences and recent-file state | `~/Library/Application Support/Convolver/` |
| Diagnostic logs | `~/Library/Logs/Convolver/` |
| Test inputs, exports, reports and scripts | One folder under `~/host1/` for the run |

Recent files are currently stored beside the log (`portable/recent_files.cpp:40`).
That conflates persistent UI state with disposable diagnostics. A small shared
location helper and a one-time read/migration fallback would be sufficient;
respect sandbox paths and platform conventions. This is lower priority than
making loading work. No broad relocation or cleanup of existing user files is
needed.

## Boundaries worth preserving

- Keep .cfg compatibility, nested lists, Windows separators and relative-path
  resolution. Do not require renaming old configurations or lists to a new suffix.
- Detect and validate file contents; filename extensions should guide the picker
  and output format, not stand in for container validation.
- Keep native .vstpreset, .aupreset and LV2 formats for host interoperability.
- Preserve embedded-filter project state so reopening does not depend on the
  original IR locations.
- Keep full paths and exact errors in copied diagnostics; use a short name and
  concise status in the ordinary window. Do not remove useful evidence merely to
  shorten the interface.
- The test-only edits to Audacity's user preset registry are a qualification
  workaround, not a proposed installation feature or supported user workflow.

Recommended order: fix the stale instructions and naming, unify input loading
and host-format validation, then tidy persistent-state locations. This is a small
boundary refactor and documentation correction, not a file-management redesign.
