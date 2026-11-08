<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-6 responsiveness qualification of 0.12.5

> **Dated record.** jrp's [responsiveness qualification](../../develop/releasing.md#responsiveness-qualification)
> of the 0.12.5 source (the commit tagged `v0.12.5`, `4117a6d`, candidate
> [run 42437291088](https://github.com/jrp2014/convolver/actions/runs/42437291088))
> on his MacBook Pro, run early on 8 November 2026. His result file
> (`rel3-0.12.5/evidence/REL6-result.md`), posted in the project the same
> day at 17:45 UTC, is copied verbatim below the line; the evidence
> directory and `rel6-evidence.zip` it names (outputs, callback JSON,
> source manifest, machine conditions) stay on his Mac. It qualifies that
> source under the recorded conditions. One condition departs from the
> procedure's "nothing else open": at jrp's direction his usual
> applications stayed open. jrp decided the result stands for the final
> candidate while no engine, loading or plug-in code changes after v0.12.8
> ([decisions](../../project/decisions.md#releases),
> [status, gate 5](../../project/status.md#what-still-blocks-the-first-preview)).
> Before the line, jrp's summary from his release-readiness review (17:37
> UTC) is kept as he wrote it.

> It says REL-6 has not run on your Mac, but the local result records all
> 24 combinations passing first time on 0.12.5. The worst host hold was
> 43.2 ms, and the longest judged callback 2.95 ms, within the prescribed
> limits. This establishes that candidate's result under the recorded
> conditions; it does not automatically qualify every subsequent binary.

## Evidence files on jrp's Mac

The run took place between 02:10 and 02:29 on 8 November 2026 (local time),
from jrp's listing of `rel3-0.12.5/evidence/` posted at 17:46 UTC.
These files and `rel6-evidence.zip` stay on his Mac; `rel6-evidence.zip`
goes with the release evidence:

| File | Bytes |
| --- | ---: |
| `REL6-result.md` (below) | 1,564 |
| `timing-summary.json` | 2,112 |
| `vst3-cold-load-timing.json`, `vst3-warm-load-timing.json` | 80,270; 119,380 |
| `au-cold-load-timing.json`, `au-warm-load-timing.json` | 80,518; 119,454 |
| `cold-ctest.txt`, `warm-vst3.txt`, `warm-au.txt` | 9,964; 4,986; 4,976 |
| `auval.txt` | 5,182 |
| `configure.txt`, `build.txt`, `sdk-unpack.txt` | 1,994; 51,156; 2,324 |
| `source-manifest.json`, `source-verification.json` | 129,345; 256 |
| `machine-preparation.txt`, `conditions-start.json`, `power-end.txt` | 1,431; 98,862; 106 |

Below the line, jrp's file as he posted it (its top heading is one level
lower here).

---

## REL-6: Convolver 0.12.5 on jrp’s Mac

8 November 2026. Candidate run 42437291088, source
`4117a6dd099fcd6d4475f129372793a5a1612653`, built from its verified
corresponding-source bundle, Release, Apple Clang 21, Accelerate.
Mac17,6, Apple M5 Max, 128 GiB, macOS 27.2 (26B123).

All 24 workload/entry combinations passed on their first attempt: VST3 and
AU, cold and warm, 1/10/30-second stereo filters, 48,000 Hz, 512-frame
callbacks. Both CTest timing tests passed; both direct warm runs exited 0.
No failures, unqualified entries, retries or late callbacks in judged phases.

| Format and history | Worst host hold | Longest judged callback |
| --- | ---: | ---: |
| VST3 cold | 38.736 ms | 0.226 ms |
| AU cold | 42.213 ms | 0.620 ms |
| VST3 warm | 39.196 ms | 2.946 ms |
| AU warm | 43.211 ms | 2.394 ms |

Host holds remained below 100 ms for 1/10-second filters and 250 ms for
30-second filters. Judged callbacks remained below 5.333 ms. Warm-up phases
are reported, not judged; their maximum was 3.923 ms.

Conditions: AC power confirmed before and after, normal power mode, sleep
prevented. Audio hosts and photo editors were closed. At jrp’s explicit
direction, usual applications including Safari, Claude and Activity Monitor
remained open. This is a passing measurement under those approved conditions,
not evidence of an all-other-applications-closed session.

Full outputs, raw callback JSON, source manifest and machine conditions are in
this evidence directory and ../rel6-evidence.zip. No host or listening pass
is inferred from this result.
