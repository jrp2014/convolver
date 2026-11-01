<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

> **Archived plan — completed.** Moved on 1 November 2026 from `docs/project/plans/2026-10-24-diagnostics-by-surface.md`. jrp decided the proposals on 31 October 2026 ([decisions](../../project/decisions.md#diagnostics)), and [PR #96](https://github.com/jrp2014/convolver/pull/96) implemented proposals 1 to 6 and 8; proposal 7, what named hosts show, is HOST-1 and AU-4 in [TODO](../../../TODO.md). Current behaviour: [command line](../../use/command-line.md), [presets](../../use/presets.md) and [plug-ins](../../use/plugins.md#status-and-the-convolver-log). The tables below record what each surface did on 24 October 2026 and were not updated; for current behaviour read the guides, not this plan.

# Diagnostics by surface

Status: review and proposals, dated 24 October 2026, written against `main` at
`b559c52` (0.4.43) with [PR #73](https://github.com/jrp2014/convolver/pull/73)
applied. It serves DIAG-1 in [TODO](../../../TODO.md). The tables record what
each surface did on that date; the proposals at the end are for jrp to decide,
and nothing in them is implemented.

**In short:**

- Every failure a user can meet is explained somewhere in words that say what
  failed and what to change. The plug-ins explain each one in the Convolver
  log (VST3, AU) or the host's log (LV2), and their Status parameter or
  `status` port says it in one line.
- The command-line tools explain configuration, file and channel failures
  generously, but three kinds of message are short and give no next step:
  invalid or overflowing samples in the CLI, a damaged sound file, and some
  `convolver-preset` messages. A `convolver-preset` option written after the
  input gets a misleading message.
- A few plug-in refusals are logged nowhere: a sample rate or block size the
  VST3 cannot use, a channel layout of more than 8 channels, an LV2 instance
  refused at instantiation, and a failed save in any format. Each is a host
  negotiation that rarely reaches a user, but none says why.
- What named hosts show (REAPER, Audacity, GarageBand) is not recorded yet;
  the [host tests](../../develop/host-testing.md) ask for it under HOST-1.

Contents: [how this was checked](#how-this-was-checked) ·
[plug-ins](#plug-ins) · [command-line tools](#command-line-tools) ·
[gaps and proposals](#gaps-and-proposals)

## How this was checked

- **Command-line tools:** each failure was run with the Linux Clang
  sanitizer build of the commit above on small fixtures made from
  `examples/first-use/` (a missing IR, a truncated WAV, a wrong rate, channel
  counts and syntax, a 9-channel filter, NaN input, a 3e38 IR, existing and
  unwritable outputs). The messages quoted are what the tools printed;
  machine-specific paths are shortened.
- **Plug-ins:** from the adapters' code (`plugins/vst3/processor.cpp`,
  `plugins/au/effect.cpp`, `plugins/lv2/plugin.cpp`) and their tests. The
  texts come from `HostFilter` and `host_status()` in
  [`host.hpp`](../../../include/convolver/host.hpp), shared by all three
  formats. The Audio Unit was not run for this review; its tests run on the
  macOS CI.
- **Named hosts:** not checked. What a host does with an error code is up to
  the host, and the Status parameter may not be visible in it.

## Plug-ins

Each row is a `HostFailureKind` (or a processing fault) and what each format
does with it. "Log" is the Convolver log for VST3 and AU
([locations](../../use/plugins.md#status-and-the-convolver-log)) and `log:log`
for LV2. The log line always carries the full explanation (what failed, why,
what to change) and the request number; the Status line is the short form of
the same text.

| Failure | VST3 | Audio Unit | LV2 |
| --- | --- | --- | --- |
| Filter file missing or unreadable (`filter_file`) | Not reachable: presets embed the filter | Not reachable: presets embed the filter | Worker refuses the file: `status` Load failed, log names the file and why it could not be used |
| Damaged state or preset (`state_unreadable`) | `setState` returns `kResultFalse`; Status Load failed; log | `RestoreState` returns `kAudioUnitErr_InvalidPropertyValue`; Status Load failed; log | `restore` returns `LV2_STATE_ERR_BAD_TYPE` or `_UNKNOWN`; `status` Load failed; log |
| State from a newer Convolver (`state_newer`) | As damaged state; the log says a newer Convolver saved it and gives the state version | As damaged state | As damaged state |
| Wrong sample rate (`sample_rate`) | Refused while playing (`kResultFalse`), or silence when found at activation; Status names both rates; log | The same, with `kAudioUnitErr_InvalidPropertyValue` | Refused at restore or file load, the session's rate being known; `status` Load failed; log |
| Wrong channel counts or layout (`channels`) | As the rate, naming both counts | As the rate | As the rate |
| Speaker labels differ (notice, plays) | Status ends "Speaker labels differ; see the Convolver log."; log names the speakers | No speaker arrangement to compare | No speaker arrangement to compare |
| Preparation failed (`preparation`) | Refused or silence; Status; log | Refused or silence; Status; log | Worker refuses; `status` Load failed; log |
| Activation failed (`activation`) | `setActive` returns `kResultFalse`; Status Load failed; log | `Initialize` returns `kAudioUnitErr_FailedInitialization`; Status; log | `activate` has no result: plays silence; `status` Load failed; log |
| Saved failure restored (`saved_failure`) | Silence; Status Load failed; log says the project was saved after a failed load | The same | The same |
| Processing fault (overflow, invalid input, engine failure, AU invalid call) | Audio thread writes Status Fault (unless Load failed is shown); log explains on the controller's request, save, restore or activation | Render sets Status Fault (unless Load failed is shown); log explains on the next text request, save, restore, initialization or cleanup | `status` Fault in the same `run()`; worker logs it |
| Rate or block size the plug-in cannot use | `setupProcessing` returns `kInvalidArgument`; **no log, Status unchanged** | Different input and output rates: `Initialize` refuses and logs | Non-integer or out-of-range rate: `instantiate` returns null; **no log** |
| More than 8 channels, or a second bus | `setBusArrangements` returns `kResultFalse`; **no log** (the host offers other layouts) | Declared channel capabilities; the host does not offer it | Fixed layouts; the host does not offer it |
| Saving fails (out of memory, stream refuses) | `getState` returns `kResultFalse`; **no log** | `SaveState` returns an error; **no log** | `save` returns `LV2_STATE_ERR_UNKNOWN`; **no log** |
| Log file cannot be written | Status text ends "The log could not be written."; lines go to standard error (and the macOS system log, `OutputDebugString` on Windows) | The same | Not applicable: LV2 writes to the host's log |

Tests: `native.host` covers every failure kind in `HostFilter` and the status
text; the VST3, AU and LV2 adapter tests (`vst3.adapter`, `au.adapter`,
`lv2.adapter`) cover damaged and newer state, rate, channels, activation and
processing faults through each adapter's own calls, and LV2 also covers
filter files and preparation. The three refusals marked **no log** have no
message to test.

## Command-line tools

`convolver` (render, `--inspect` and `--validate`), `convolver-preset` and
`convolver-equalizer-apo` print to standard error and exit with status 1 on
every failure below. `--validate --json` and `--inspect --json` also report
each diagnostic with a code (shown in brackets in the text output).

| Failure | `convolver` | `convolver-preset` | `convolver-equalizer-apo` |
| --- | --- | --- | --- |
| IR file missing | `convolver: invalid configuration: missing.cfg:8:1: filter 'missing.wav': cannot open audio input missing.wav: ... (looked for /…/missing.wav; IR file names are relative to the folder containing the configuration, and letter case must match ...)`; `--validate`: `[missing-asset]` | The same text, without a program name | The same text under "missing.cfg cannot be exported until these are fixed:" |
| IR file damaged | `... filter 'corrupt.wav': truncated or invalid RIFF/WAVE size` (no next step) | The same | The same |
| Syntax error | `... syntax.cfg:3:1: delay count differs from channel count (1 values for 2 channels)` | The same | The same |
| Wrong sample rate | Names both rates, says Convolver never resamples, and suggests a filter at the audio's rate, converting the audio, or a configuration list; `--validate`: `[no-match]` | The same, but says "the audio to be filtered (48000 Hz)" for the `--sample-rate` choice | Not an error: each entry is exported for its own rate |
| Wrong channel count | Names both counts and what to use; for a mono IR it prints a ready-to-save `.cfg` | The same | Not an error: the device's channels decide at playback |
| More than 8 channels for a plug-in | Not a limit | `AU presets support 1 to 8 input and output channels; this filter has 9 inputs and 9 outputs` (no next step) | Exported (Equalizer APO allows 32) |
| Not a preset file to convert | Not applicable | `not a VST3 preset file (no 'VST3' header)` (no file name, no next step) | Not applicable |
| Unknown output kind | Output format chosen by option | `p5.txt: choose the preset format by the name's ending: ...` | Not applicable |
| Option after the input | Options may come anywhere | `--sample-rate: choose the preset format by the name's ending: ...` (misleading: the option is taken as an output name) | Not applicable |
| Input audio missing | `convolver: cannot open audio input nope.wav: System error : No such file or directory.` | Not applicable | Not applicable |
| Output exists | **Replaced without a message** ([output safety](../../use/command-line.md#output-safety)) | `e.vstpreset already exists; presets are only written to new paths, so choose another name or remove it first` | The same for the folder |
| Output cannot be created | `convolver: cannot open audio output /nonexistent/o8.wav: System error : No such file or directory.` | `/nonexistent/x.vstpreset: cannot create output file (must be a new path). Check that its folder exists and is writable.` | `cannot create /nonexistent/eapo2. Check that its parent folder exists and is writable.` |
| Invalid input samples (NaN, infinite) | `convolver: nonfinite audio sample` (no file, position or next step); a partial output file is left | Not applicable | Not applicable |
| Output overflow | `convolver: nonfinite output (input/filter/gain overflow)` (no position or next step); a partial output file is left | Not applicable | Not applicable |
| FFTW option with another backend | `convolver: tuning and wisdom require the FFTW backend` | Not applicable | Not applicable |

Tests: `native.cli` (`tests/native/cli_tests.py`) checks exit status and
many messages, `plugins.presets` (`tests/plugins/preset_tests.py`) the preset
tool, and `export.equalizer_apo` the export tool; the configuration messages
themselves are checked in `native.config_parse` and `native.inspection`. No
test checks that every row above names a next step.

## Gaps and proposals

Each proposal keeps the existing texts and adds to them. None changes what
plays or which files are accepted.

1. **CLI sample faults.** Make "nonfinite audio sample" and "nonfinite output"
   as explanatory as the plug-ins' fault message: the file, the time (frame
   and seconds) and channel of the first bad sample, and what to change
   ("the input file contains invalid samples; re-export it" or "lower
   `--gain-db` or the filter's gain"). Code: `portable/io.cpp`,
   `portable/main.cpp`.
2. **Partial CLI output.** After a failure the CLI leaves a truncated output
   file, which is documented ([output safety](../../use/command-line.md#output-safety))
   and listed in TODO's documented limitations. At least say so in the
   message ("OUTPUT is incomplete; delete it"); removing a regular output
   file on failure, or writing to a temporary file and renaming it, is the
   larger change already noted there. jrp to choose.
3. **Damaged sound files.** Add a next step to libsndfile and header errors:
   "the file is damaged or not a sound file; export it again from the program
   that made it". Applies to all three tools.
4. **`convolver-preset` wording.** Start its messages with `convolver-preset:`
   as the other tools do; name the file in "not a VST3 preset file"; add "use
   the command-line tool for wider filters" to the 8-channel limit; say "the
   requested sample rate" instead of "the audio to be filtered"; and accept
   options after the input, or say that options go before it.
5. **Silent plug-in refusals.** Log one line, outside the control lock, when
   VST3 refuses a sample rate or block size in `setupProcessing` or a layout
   in `setBusArrangements`, and when LV2 refuses a rate in `instantiate` (its
   log feature is already available there). Log a failed save in all three
   formats. Hosts often probe layouts, so a refused layout would be logged
   once per instance, not per call.
6. **CLI replaces existing outputs.** The CLI overwrites an existing output
   file; both other tools refuse. This is long-standing behaviour that scripts
   may rely on, so the proposal is only to record it as deliberate in the
   command-line reference. jrp to decide whether it should change.
7. **Named hosts.** Record under HOST-1 and AU-4 what REAPER, Audacity and
   GarageBand show for a refused preset, a silent first load and a Fault, and
   whether they show the Status parameter, as the
   [host tests](../../develop/host-testing.md) already ask.
8. **Tests for every row.** Add one table-driven test per tool
   (`native.cli`, `plugins.presets`, `export.equalizer_apo`) that runs each
   failure above and checks that its message names the file or value and a
   next step, so a later change cannot drop the explanation.
