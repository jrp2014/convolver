# Archived review documents

These assessments were superseded on 18 October 2026 by a repository review
based on portable 0.2.0 at `5df874e`, kept at the root as `REVIEW.md` until
23 October 2026 and now [archived here](2026-10-20-repository-review.md). The
current status is in [status](../../project/status.md). These records preserve
how the project reached its present state.

| Record | Original scope | Successor |
| --- | --- | --- |
| [4.3 local review](4.3-local-review.md) | SourceForge 4.3 archive and a separate local patched source snapshot | [Implementation map](../../develop/architecture.md) |
| [4.3 branch review](4.3-branch-review.md) | Python model of 4.3 and initial review-branch fixes | [Current review entry point](../../review-2026-10/convolver-review.md) |
| [Original modernization assessment](2026-10-17-modernization.md) | Initial architecture/options assessment at `b3b479f`, with incremental early-port updates | [Status: direction](../../project/status.md#direction) |
| [Early portable structure review](early-portable-structure.md) | FFT abstraction and packaging milestone, with later partial updates | [Implementation map](../../develop/architecture.md) |
| [Repository review, 20 October 2026](2026-10-20-repository-review.md) | Status guide mapping implemented behaviour to source and checks at `2fe503e`; root `REVIEW.md` until 23 October 2026 | [Status](../../project/status.md), [implementation map](../../develop/architecture.md) |
| [Documentation review, 20 October 2026](2026-10-20-documentation-review.md) | Reorganized the maintained guides at `2fe503e` (0.4.0); pre-review copies are in [documentation-2026-10-20](../documentation-2026-10-20/README.md) | [Documentation guide](../../develop/documentation.md) |
| [RequestWorker and its watchdog, 1 to 5 November 2026](2026-11-05-request-worker.md) | The Audio Unit's Latency thread, its tests and the CI-22 to CI-25 evidence, until AU-12 replaced it with a dispatch source | [Plug-in contract: Audio Unit](../../develop/plugin-contract.md), [testing](../../develop/testing.md#intermittent-failures) |
| [HOST-1 REAPER session, 6 November 2026](2026-11-06-host1-reaper.md) | jrp's first REAPER session with candidate 0.10.9 (`83aa64d`) on macOS arm64: the stereo journey passed; the Latency change and the 2→6 crossover did not work as the guides said | [Crossover retest](2026-11-07-reaper-crossover-retest.md), [VST3-16 acceptance](2026-11-08-vst3-16-reaper-acceptance.md) |
| [VST3-16 REAPER latency trace, 7 November 2026](2026-11-07-vst3-16-reaper-trace.md) | jrp's trace of a Latency change in REAPER 7.85 with PR #134's trace build (`d7d16d2`): REAPER reads `getLatencySamples()` inside `restartComponent(kLatencyChanged)` and does not restart the plug-in, so the old Latency stayed until a render or FX Offline then Online; every render lined up | [Plug-in contract: settings](../../develop/plugin-contract.md#settings) |
| [VST3-16 REAPER acceptance, 8 November 2026](2026-11-08-vst3-16-reaper-acceptance.md) | jrp's check of the VST3-16 fix with PR #134's candidate (`7d6a8b4`) in REAPER 7.85: a new Latency, stopped and while playing, in both directions, showed at once in the window and in REAPER's delay compensation, and every impulse in the later renders lined up; listening during the change was not checked | [Plug-in contract: settings](../../develop/plugin-contract.md#settings) |
| [REAPER window and settings, 8 November 2026](2026-11-08-host1-reaper-window.md) | jrp's REAPER 7.85 check with the 0.12.5 candidate (`4117a6d`): the VST3-16 latency changes again lined up in every render, and part C on the redesigned window passed its controls and state (Choose…, Gain, Mix, undo, reset, details, Copy Diagnostics, save, quit and reopen with Gain, Mix and Latency kept); listening not checked | [Host testing: REAPER](../../develop/host-testing.md#reaper-vst3) |
| [PLUG-31 REAPER check, 8 November 2026](2026-11-08-plug31-reaper.md) | jrp's REAPER 7.85 check of the filter's name in generated VST3 presets with the candidate from `f11a3dd` (PR #148): the packaged identity preset showed `impulse.wav`, also after save and reopen, the first-use stereo preset `filter.cfg`, and a preset without its name `Restored filter`; all 21 packaged presets carry a name; listening not checked | [Plug-in contract: preset files](../../develop/plugin-contract.md#preset-files) |
| [GarageBand Audio Unit checks, 8 November 2026](2026-11-08-au4-garageband.md) | jrp's GarageBand 10.4.14 check of the Audio Unit from the 0.12.5 candidate (`4117a6d`): presets, Choose…, a refused 48 kHz preset that kept the filter, Gain, Mix and Latency kept after save and reopen, and two echo exports matching the CLI; listening, bypass clicks, live latency compensation and VoiceOver not checked | [Host testing: GarageBand](../../develop/host-testing.md#garageband-audio-unit) |
| [REAPER crossover retest, 7 November 2026](2026-11-07-reaper-crossover-retest.md) | jrp's retest of HOST-1 part D with PR #131's candidate (`5d188a7`): render, Mix 0%, bypass, refused preset and wrong rate passed; save and reopen, and FX Offline then Online, lost the crossover; the second retest at `c3dbf0d` passed them | [Host testing: REAPER](../../develop/host-testing.md#reaper-vst3) |
| [HOST-1 Audacity session, 7 November 2026](2026-11-07-host1-audacity.md) | jrp's Audacity 4.0.2 session with the VST3 plug-in from PR #131's candidate (0.10.13, `c3dbf0d` built as merge `1b0d873`) on macOS arm64: every stereo export with a filter or bypass matched the command line, including after a refused preset and a restart; Audacity's preset menu does not import `.vstpreset`, and its playback ran at another rate than its export | [Host testing: Audacity](../../develop/host-testing.md#audacity-vst3) |
| [File-loading review, 7 November 2026](2026-11-07-file-loading-review.md) | jrp's review of how filters, presets and their folders are loaded, written with the Audacity session: one dependable way to load a file the user already has, a chosen file checked when the host's audio settings are known, shorter instructions, named packaged presets, and the Recent list out of the log folder | PLUG-25 to PLUG-29 and DOC-10 in [TODO](../../../TODO.md) |

The first two reports describe different intermediate patches, not the final
merged implementation. The [reconciliation](../../review-2026-10/reconciliation.md)
records the actual merge: the FastArray overread was undefined behavior (not
harmless), and partition gaps depend on padded stride, not simply on using more
than one partition. Modern overlap-add code must be assessed independently of
the old overlap-save implementation.

The local review was previously an ignored workspace file. Its original local
`source/`, `review/`, `archives/` and early test-runner references are preserved as
historical path text, not working checkout links. Those private working artifacts
are not imported here. Use the tracked reconciliation, regression harnesses and
provenance records for reproducible checks of the merged source. The branch
review's Python model/results remain at their original tracked paths.

Archive copies retain the old assessments, including superseded recommendations
and limitations. Only the archive notice, link locations and personal-name
redaction were changed; no old test result is being presented as a fresh run.
The 4.4 website archive and its preservation manifests remain unchanged.
