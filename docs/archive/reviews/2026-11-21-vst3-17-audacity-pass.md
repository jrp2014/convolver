<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# VST3-17 Audacity check passed on 0.16.14, 21 November 2026

> **Dated record.** jrp's second run of the
> [check of VST3-17's fix](../../project/status.md#checks-left-before-publication)
> (check 10), on the candidate built from `main` after PLUG-27's fix
> ([PR #233](https://github.com/jrp2014/convolver/pull/233)): version
> 0.16.14, commit `4d78868`, [candidate run
> 44904627283](https://github.com/jrp2014/convolver/actions/runs/44904627283),
> following the no-playback script in the project files
> (`vst3-17-audacity/`, parts A, B, L and C). Posted in the project on 21
> November 2026 at 16:42 UTC and copied below the line (its top heading is
> one level lower here; links to files on jrp's Mac are given as their
> names). All four required exports passed: a filter chosen with
> **Choose…** before playback reached the export with the window open and,
> after saving, quitting and reopening without opening the window, the
> next export too; **Recent** worked; and a configuration list chosen again
> after one playback took its 48 kHz entry. The [first run, on
> 0.14.0](2026-11-20-vst3-17-audacity.md), failed at the choice itself
> (PLUG-27).
>
> Two observations are follow-ups, not failures: after playback, the list's
> earlier refusal still said the host's settings were unknown until the
> list was chosen again, and the window's "Or drop a file on this window."
> after that refusal does not fit a message that says to play first. Undo
> right after a choice removes the whole effect in Audacity 4.0.2 (its only
> Undo item was "Undo 'Add Convolver'"), so part C could not show whether
> undo brings back an earlier filter.

---

## The revised Audacity walkthrough passed all four required exports

On Convolver **0.16.14**, choosing a filter before playback worked, the
saved project restored it after a full quit and reopen, Recent worked, and
the configuration list selected its 48 kHz entry after one playback.

I tested Audacity **4.0.2** on macOS **27.2.1** (arm64), with Audacity's
default sample rate set to **48,000 Hz**. The installed VST3 matched the
candidate bundle from commit `4d78868772b5`. [Candidate run
44904627283](https://github.com/jrp2014/convolver/actions/runs/44904627283)

```text
9ddfb6bbd634114299c89a612eba460a60738d927d75cd6bcbd0c09213b7ff03  Convolver-0.16.14-preview-macos-arm64.zip
```

| Step | Result | What happened |
|---|---|---|
| A6 · `a1.wav` | **Pass** | Chose `filter.cfg` before playback and exported with the vendor window open. |
| A8 · `a2.wav` | **Pass** | Quit Audacity, reopened `vst3-17-a`, and exported without playing or opening Convolver. |
| B3 · `b1.wav` | **Pass** | Selected `filter.cfg` from Recent in a fresh project and exported with the window open. |
| L5 · `l1.wav` | **Pass** | After the short clip played, chose `rates.txt` again. Convolver showed **Active**, 48,000 Hz, 2 in → 2 out. The 44.1 kHz entry was first in the list. |

All four were full-project, stereo, 32-bit float WAV exports. Each had the
same largest sample difference from `expected.wav`: **7.45 × 10⁻⁸**, or
**−137.11 dB** relative to the reference peak, at channel 2, frame 3390. I
left Gain, Mix, Latency and Gain mode untouched between each choice and
export.

**A4 and A5.** The window kept `filter.cfg` named and showed **Waiting**.
Its full text was "Filter selected. Waiting for the host's audio settings.
Convolver checks the filter when the host plays audio through the
plug-in." The Edit menu said **Undo 'Add Convolver'**.

**L3.** Before playback, the window showed **Load failed** and "rates.txt:
no entry selected." Its instruction was "Play audio once so Convolver can
check the host's sample rate and channel counts, then choose the
configuration list again." One detail worth recording: after playback, the
Audio panel correctly showed **48000 Hz, 2 in, 2 out**, while the previous
error still said those settings were unknown. Choosing the list again
cleared the error. I repeated part L in a fresh Audacity process to keep
the vendor window in focus.

**C2 and C3.** After choosing the alternate echo filter, the menu still
said **Undo 'Add Convolver'**. Invoking it removed the entire effect and
closed its window, so there was no filename to report. `c1.wav` differed
from the filtered reference and matched the dry `input.wav` **exactly**,
with a largest sample difference of zero. The requested failed comparison
was:

```text
Output:   c1.wav: 48000 Hz, 2 channels, 4096 frames (85.33 ms)
Expected: examples/first-use/stereo/expected.wav: 48000 Hz, 2 channels, 7144 frames (148.83 ms)
Largest difference: 0.876 (4.30 dB relative to the expected peak of 0.534), channel 2, frame 709 (14.77 ms)
Different: samples differ by more than the limit (0.0001 of the expected peak, -80 dB, or two 16-bit steps).
Check that the output was rendered from the example's input.wav with its filter, and that nothing else on the track changed the sound.
```

Part D was unnecessary because A and B passed. I skipped the optional
0.13.2 control. The saved project and all five exports are in the
candidate folder. Saved Audacity project (`vst3-17-a.aup4`) · Session log
(`audacity-vst3-17.log`). One fluff that could be corrected is the
suggestion to drop a file onto the window when the message says that no
filter can be validated before the host plays something
