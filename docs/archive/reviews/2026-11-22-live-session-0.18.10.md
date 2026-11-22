<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Mac live session results on 0.18.10 (22 November 2026, 03:05 UTC)

jrp ran the live-session page (kept outside the repository, with its helper
script) and reported these results in the project chat. His full report and evidence ZIP
are on his Mac (`~/convolver-live/evidence/summary.md`,
`~/convolver-live/live-session-evidence-20261122-0259.zip`), where threads
cannot read them. The step IDs below are mapped from his summary. Where his
summary did not name a step, it is listed as not reported. It is not marked
as passed.

- **Build:** the v0.18.10 tag (`005a70a9c497`), built locally from source. This
  is not the release's own zip or pkg.
- **Machine:** an M5 Max running macOS 27.2.1, using the laptop speakers.
- **Hosts:** REAPER 7.86 (VST3), Audacity 4.0.2 (VST3) and GarageBand 10.4.15 (AU).

## Passed

| Step | Host | Result |
| --- | --- | --- |
| A1 (HOST-11 run 2) | REAPER | 8 of 8. Checks 3 and 7 read Status with the transport running at 1.445 s. |
| A1 checks 4 and 8, D5 | REAPER | The stereo and six-channel renders matched before and after reopening. |
| D3 (PLUG-39 step 6) | REAPER | Switching from triple to quiet kept the 1.20 s ring-out through Stop. Taking the effect offline and back online cleared it. |
| D4 (PERF-2) | REAPER | The 30-second filter at Latency 0 played a full loop with no underruns reported. |
| B2, single filter | Audacity | A single filter chosen before playback worked. |
| F2, F3 | GarageBand | The aligned dry and filtered tracks matched their predicted sum exactly at Latency 0, and within 1.19e-07 at 1024. The pending-latency indication cleared during playback. |
| G (COMPAT-1) | REAPER | Built with the published 0.14.0 VST3 and AU. All three tracks restored Gain, Mix and Latency on both versions. The 52,096-frame stereo render was identical in every sample. |

## Failed (both fixed in PR #271)

| Step | Host | Result |
| --- | --- | --- |
| E, and B2 with a list (PLUG-40) | Audacity | `rates.txt`, chosen before the first playback, stayed **Load failed** after 5.22 s of playback at 48 kHz. The export, a retry and an export after reopening were all silent. Choosing the list again after playback recovered it. Fixed by **PLUG-49**. |
| D1 | REAPER | The 44.1 kHz preset was correctly refused at 48 kHz: the slap kept playing, and the window explained why. But the shown Latency changed from 1024 to 0 (reproduced twice). Only the display was wrong, because the processor kept playing at 1024. Fixed by **PLUG-48**. |

## Walkthrough problem, not a plug-in fault

- C6: GarageBand put its metronome in the first export. The page and the
  [host-testing guide](../../develop/host-testing.md#garageband-audio-unit) now say to turn it off.

## Not verified (needs a listener at the laptop)

- Clicks or doubled attacks during live changes (A3, A5, B3, C3).
- VoiceOver reading the control names and adjusting Gain (F7).

## Not reported in the summary

A2, A4, A4b, A5–A9 (apart from the renders above), B3–B7, C1–C5, D2, D3b,
F1, F4 and later steps, and pass 3.
