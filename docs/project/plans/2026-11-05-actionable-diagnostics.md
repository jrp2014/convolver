<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Actionable diagnostics: problem, cause, fix

Status: proposal for jrp, 5 November 2026, written against `main` at `1467571`
(0.8.11). It serves DIAG-2 in [TODO](../../../TODO.md). Nothing in it is
implemented yet. It keeps DIAG-1's choices
([decisions](../decisions.md#diagnostics)): messages still explain generously,
and the CLI still replaces an existing output.

For jrp, deciding the shape of Convolver's messages, and for whoever then
rewrites them.

**In short:**

- Convolver's messages already say what failed, why and what to change, but
  each one is a single run-on paragraph. The problem is often buried behind
  prefixes ("invalid configuration: file:8:1: filter 'x': cannot open audio
  input x: System error : ..."), the fix sits mid-sentence, and some messages
  have no fix at all (a syntax error, a bad option value, a missing output
  folder).
- Proposal: every message a user can meet gets the same three parts, in this
  order: the **problem** in one sentence, its **cause**, and the **fix**; and,
  where an environment, host or design limit is the reason, a **note** with
  that context. The words stay as helpful as now; they are reordered,
  shortened and completed.
- The command-line tools print the parts on separate, labelled lines. The
  plug-in window shows the problem in bold, then the cause and the fix, and
  one button beside the fix when the window itself can do it (Choose Filter…,
  Copy Diagnostics). The Convolver log keeps one line per event, and the
  Status parameter stays one short line.
- Out of scope, unless jrp asks: error codes with a documentation page each,
  translations, colour in terminal output, and new kinds of dialog.

Contents: [what users see now](#what-users-see-now) ·
[the structure](#the-structure) · [before and after](#before-and-after) ·
[the plug-in window](#the-plug-in-window) · [how it is built](#how-it-is-built) ·
[decisions for jrp](#decisions-for-jrp)

## What users see now

Checked by running each failure with the Linux sanitizer build of `1467571`
on fixtures made from `examples/first-use/stereo/`, and from the plug-in
code (`portable/host.cpp`, `portable/host_log.cpp`,
`portable/host_report.cpp`) for what the window and the log show.

| Pattern | Example |
| --- | --- |
| The problem comes last, after prefixes | `convolver: invalid configuration: missing.cfg:8:1: filter 'missing.wav': cannot open audio input missing.wav: System error : No such file or directory. (looked for /…/missing.wav; IR file names are relative to …)` |
| A library's own words leak through | `System error : No such file or directory.` (libsndfile) |
| A library's error name leaks through | `convolver --gain-db abc` prints only `convolver: stod` |
| No fix | `syntax.cfg:3:1: expected header, two delay lines, and four lines per filter path (path 1 has 0 of 4 lines)`; `gain must be finite and representable`; `cannot open audio output /nonexistent/o.wav: System error : …`; `unknown option: --bogus` |
| The problem is the second sentence | `rate.cfg is a configuration for 44100 Hz audio with 2 input channels and 2 output channels (the first three numbers on its first non-comment line). It cannot be used with …` |
| The window's explanation joins several messages into one paragraph | `Filter not loaded; the previous filter is kept. The filter was made for 44100 Hz, but the host runs at 48000 Hz. Convolver does not resample: load a filter made for 48000 Hz, or set the host to 44100 Hz. Playing 48000 Hz, 2 in, 2 out. …` |
| The fix is in the window but far from the message | "Choose…" sits beside the Filter row; "Copy Diagnostics" at the bottom |

The good parts stay: every rate and channel message names both values and
both ways out, the mono-IR message prints a ready-to-save `.cfg`, faults say
how often and what to change, and the log carries the full story for a
report.

## The structure

| Part | What it says | Rules |
| --- | --- | --- |
| Problem | What went wrong, for which file or setting, in one sentence | Starts with the thing affected; names the file, line, value or channel; no library wording |
| Cause | Why, in the user's terms | Names both sides of a mismatch; one or two sentences |
| Fix | What to do now | Starts with a verb; the most likely fix first, then the alternative; exact values, option names and menu names |
| Note (optional) | The environment, host or design limit behind it, and where to read more | Only when it explains a constraint: "Convolver never resamples", "letter case matters on Linux", "GarageBand gives a mono track one channel", "this build has no FFTW" |

Warnings and notices (dither advice, a raw filter's assumed sample type)
keep their current one-line form: nothing failed, so there is nothing to
fix.

## Before and after

Command line (the same parts for `convolver-preset` and
`convolver-equalizer-apo`, with their own program names):

```text
convolver: error: missing.cfg:8:1: IR file 'missing.wav' not found
  Cause: there is no file at /…/fx/missing.wav. IR file names are relative to
         the folder that holds missing.cfg.
  Fix: correct the name on line 8, or put missing.wav in that folder.
  Note: letter case counts on Linux and on case-sensitive macOS volumes.
```

```text
convolver: error: filter.cfg is for 48000 Hz audio, but in441.wav is 44100 Hz
  Cause: Convolver never resamples, so a filter only fits audio at the rate it
         was made for.
  Fix: use a filter made for 44100 Hz, or convert in441.wav to 48000 Hz first.
  Note: a configuration list can hold one configuration per sample rate and
        picks the matching one.
```

```text
convolver: error: syntax.cfg:3:1: filter path 1 is incomplete (0 of 4 lines)
  Cause: each filter path needs four lines: IR file, IR channel, input
         channel(s), output channel(s).
  Fix: add the missing lines after line 3, or remove the path.
```

```text
convolver: error: cannot create the output file /nonexistent/o.wav
  Cause: its folder /nonexistent does not exist.
  Fix: create the folder, or choose an output in an existing folder.
```

```text
convolver: error: --gain-db abc is not a gain
  Cause: --gain-db takes a number of decibels (such as -6), 'no-clip' or
         'auto'.
  Fix: write the gain as a number, such as --gain-db -6, or use
       --gain-db no-clip to keep full-scale input within full scale.
```

Plug-ins, in the window (and as one line in the Convolver log):

```text
[bold] The new filter was not loaded: it is for 44100 Hz, and the host runs at 48000 Hz.
       The previous filter keeps playing.
Fix:   Choose a filter made for 48000 Hz, or set the host's project to 44100 Hz.
       [Choose Filter…]
Note:  Convolver never resamples.
```

```text
[bold] Fault: the filter failed while processing, so Convolver silenced the
       audio and restarted it (3 times).
Cause: this is a fault in Convolver, not in your audio or settings.
Fix:   Copy the diagnostics and send them with a short description.
       [Copy Diagnostics]
```

The Status parameter, which hosts show in a small field, keeps its one line:
problem first, then "See the Convolver log." ("Load failed, previous filter
kept: the filter is for 44100 Hz, the host runs at 48000 Hz. See the Convolver
log.").

## The plug-in window

- The explanation area shows the problem in bold, then the cause, then
  **Fix:** and **Note:** with their labels in bold, as separate paragraphs,
  with the system's fonts and colours (secondary colour for the note), so
  VoiceOver reads them in order and dark mode works.
- One action button under the fix when the window can carry the fix out:

  | Situation | Button |
  | --- | --- |
  | No filter yet, a refused or unreadable filter file, a saved failure, a filter that does not suit the host | Choose Filter… (the same action as Choose…) |
  | A processing failure (a Convolver bug) | Copy Diagnostics |
  | Anything else (rate set in the host, mono track, overflow, log not writable) | None: the fix says where to change it |

  This follows Apple's guidance to put a fix next to the message that needs
  it and name the button with a verb (Human Interface Guidelines, "Alerts"
  and "Feedback"), without an alert sheet: a plug-in window must not block
  the host. Choose… and Copy Diagnostics stay where they are as well.
- The Audio facts, the Filter row's "was refused" note and the folded
  reference lines do not change.

## How it is built

- **One text convention, no new exception types.** A message is its problem
  sentence, then lines that start "Cause:", "Fix:" and "Note:". Exceptions
  carry that text as now, so the configuration loader's location prefix
  still lands on the problem line, and messages that do not use it yet still
  print as they did. One small internal header formats it: the CLI tools
  indent the labelled lines under `program: error: problem`; the log and
  `--validate --json`'s `message` join it into one line; the window's report
  gives the parts as separate fields.
- **Content:** each user-reachable message in `portable/` (configuration
  parsing and selection, sound-file reading and writing, CLI options,
  `convolver-preset`, `convolver-equalizer-apo`, the plug-in failure, fault
  and status texts) is rewritten into the parts. Internal invariant checks a
  user cannot reach keep their wording.
- **Window:** `host_report` adds the parts and the action to the report's
  fields; `diagnostic_view.mm` lays them out and adds the button. The AU and
  VST3 editor tests on the macOS CI check the button and its action.
- **Tests:** one table-driven test per tool runs every failure in the
  [diagnostics by surface](../../archive/plans/2026-10-24-diagnostics-by-surface.md)
  table and checks that the message has a problem line and a `Fix:` line
  naming the file or value; `native.host_report` checks the window fields and
  action for each failure kind.
- **Documentation:** [troubleshooting](../../use/troubleshooting.md),
  [command line](../../use/command-line.md), [presets](../../use/presets.md#messages)
  and the window section of [plug-ins](../../use/plugins.md#convolvers-window)
  show the new form.
- **Size:** about 60 user-reachable messages and their tests; one commit for
  the convention and the CLI, one for the plug-ins and the window, one for
  the documentation.

## Decisions for jrp

1. Adopt the problem, cause, fix and note structure, with labelled lines in
   the command-line tools (recommended), or only rewrite the wording inside
   today's one-paragraph form.
2. Add the window's one action button (Choose Filter… or Copy Diagnostics)
   beside the fix (recommended), or keep the existing buttons only.
