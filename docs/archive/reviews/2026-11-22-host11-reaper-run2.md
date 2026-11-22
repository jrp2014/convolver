<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-11 second REAPER run, 22 November 2026

> **Dated record.** jrp's second run of `scripts/reaper_check.py`
> ([REAPER from one command](../../develop/host-testing.md#reaper-from-one-command)),
> as step A1 of his Mac live session. He used REAPER 7.86 on macOS 27.2.1
> (M5 Max) with v0.18.10 (`005a70a9c497`) built on his Mac from the tag,
> the source of the preview published at 02:11 UTC that day. The script was
> the one on `main` after PRs #251 and #255: it reads Status while REAPER's
> transport runs, and it accepts only the exact Active text. The REAPER
> results come from his session summary, posted in the project on
> 22 November 2026 at 03:05 UTC, and its line on this check is copied below
> the line. Its `result.md` stayed on his Mac.
>
> All eight checks passed. Both readings "while playing" were taken with the
> transport running, at a play position of 1.445 s. The script records
> REAPER's `GetPlayPosition`, which allows for the device's output latency,
> so it can read a little under the 1.5 s the script plays before reading;
> the summary itself does not give the cause. The script's checks need
> only the transport running, not a position. HOST-11's "Done when" asks
> for a run on a published preview whose result file records what every
> report lists. This run differs in two ways: the package was built from
> the published tag rather than downloaded, so its SHA-256 is not the
> release asset's, and this record rests on the session's summary, not on
> the run's `result.md` (with the FFT backend, block size and log lines). jrp accepted the run as closing HOST-11 on 22 November 2026.
>
> The same session found other things that are not this check's. A refused
> 44.1 kHz preset in REAPER set Latency from 1024 frames to 0. A list chosen
> in Audacity before playback stayed refused (PLUG-40). GarageBand's
> metronome was included in its export. Those findings are left out of
> this record; the planned REAPER check of a refused preset is HOST-13.

---

| Test | What happened |
|---|---|
| **REAPER — rendering and ring-out** | HOST-11 passed **8/8**. Both live Status readings were taken with the transport running at **1.445 s**, just short of the older 1.5 s instruction. Stereo and six-channel renders matched before and after reopening. Switching from triple to quiet kept the 1.20 s ring-out through transport Stop. Taking the effect offline and online cleared it. The 30-second filter ran through a full loop at zero added latency, with no reported underruns. |

I tested the requested **0.18.10 tag `005a70a9c497` built locally** on the
M5 Max running macOS 27.2.1.
