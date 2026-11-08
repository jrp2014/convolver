<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REAPER window and settings (HOST-1 part C), 8 November 2026

> **Dated record.** jrp's REAPER 7.85 checks of the VST3 plug-in with the
> 0.12.5 candidate from `main`
> ([run 42437291088](https://github.com/jrp2014/convolver/actions/runs/42437291088),
> source `4117a6d`, the commit tagged `v0.12.5`), posted in the project on
> 8 November 2026 at 13:18 UTC and copied here verbatim below the line. It
> repeats the VST3-16 latency check on this candidate and runs part C of the
> [REAPER smoke test](../../develop/host-testing.md#reaper-vst3) (step 11)
> on the window PLUG-22 redesigned. The files it names are on jrp's Mac and
> are not in this repository. Listening was not checked. PLUG-31
> ([PR #148](https://github.com/jrp2014/convolver/pull/148)), merging before
> the preview, changes the candidate after this run; see
> [status](../../project/status.md#what-still-blocks-the-first-preview).

---

# REAPER 7.85, Convolver 0.12.5

Candidate run 42437291088, source 4117a6dd099fcd6d4475f129372793a5a1612653.
macOS 27.2, arm64. Project latency-preview.rpp is a copy of the existing
identity-filter impulse test: 48,000 Hz, stereo, 512-frame host block, unity
track gains, quiet master monitoring.

- Restored identity filter at 256 frames; Active, 5.3 ms, REAPER 256/512 spls.
- Changed to 1024 while stopped using Convolver’s menu. Immediately showed
  21.3 ms and REAPER 1024/1024 spls, before rendering or Offline/Online.
- Rendered separate stereo stems as 32-bit float WAV, full project, 100 ms
  tail, 48,000 Hz, no dither or postprocessing. Every impulse offset zero;
  stereo CLI comparison passes, maximum sample error 1.19e-7.
- Visually confirmed running playback, Repeat on, both track meters active.
- Changed 1024 to 256 while playing: 5.3 ms and 256/512 spls before stop/render.
- Changed 256 to 1024 while playing: 21.3 ms; 1024/1024 spls confirmed while
  playback continued, after closing/reopening the FX window (no stop or
  Offline/Online). The immediate AX snapshot still showed 256/512; the
  subsequent screenshot confirmed the updated display.
- Both resulting stereo exports match; all four first-channel impulses align
  exactly. Worst errors: 5.96e-8 at 256; 1.19e-7 at 1024.
- Project saved, playback stopped. Listening to the transition gap unverified.
- Remaining: new-window checks and save/reopen.

Automation cannot reliably operate REAPER’s main window or transport; native
plug-in and render dialogs respond. This is a tool limitation, not evidence
of a Convolver failure.

## Window part C, 8 November 2026

Convolver 0.12.5 in REAPER 7.85, macOS arm64. Loaded packaged
`examples/first-use/stereo/filter.cfg` using Choose; Active, 3001 taps,
48 kHz stereo, filename and routing displayed. Gain drag reached -21.3 dB;
one host Edit FX parameter Undo restored 0 dB (closing the FX window
created a separate Close FX chain undo entry first). Mix drag reached
0.2%, numeric entry reached exactly 0%. Option reset restored Gain 0 and
Mix 100. Gain entry `-3,5` produced -3.5. Expanded details were visible
without clipping and collapsed again. Copy Diagnostics worked; the report
is saved in window-diagnostics.txt. Immediate copying in the same automated
batch as setting values captured the preceding refresh; a second copy after
the displayed values updated captured -6 dB and 70% correctly.

Saved window-preview.rpp with Gain -6 dB, Mix 70%, latency 1024. Quit REAPER
normally, relaunched, and reopened its FX window: Active, filter.cfg,
3001 taps, -6 dB, 70%, 1024 frames, 21.3 ms and host 1024/1024 spls all
restored. Reload is unavailable for this embedded restored state, as documented.

Listening checks for clicks, dry sound and the latency gap remain unverified;
this run establishes control/state behavior and the separate latency renders,
not a subjective listening result.
