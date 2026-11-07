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
| [HOST-1 REAPER session, 6 November 2026](2026-11-06-host1-reaper.md) | jrp's first REAPER session with candidate 0.10.9 (`83aa64d`) on macOS arm64: the stereo journey passed; the Latency change and the 2→6 crossover did not work as the guides said | [Host testing: unresolved findings](../../develop/host-testing.md#unresolved-host-findings) |
| [REAPER crossover retest, 7 November 2026](2026-11-07-reaper-crossover-retest.md) | jrp's retest of HOST-1 part D with PR #131's candidate (`5d188a7`): render, Mix 0%, bypass, refused preset and wrong rate passed; save and reopen, and FX Offline then Online, lost the crossover; the second retest at `c3dbf0d` passed them | [Host testing: unresolved findings](../../develop/host-testing.md#unresolved-host-findings) |
| [HOST-1 Audacity session, 7 November 2026](2026-11-07-host1-audacity.md) | jrp's Audacity 4.0.2 session with the VST3 plug-in from PR #131's candidate (0.10.13, `c3dbf0d`) on macOS arm64: every stereo export matched the command line, including after a refused preset and a restart; Audacity's preset menu does not import `.vstpreset`, and its playback ran at another rate than its export | [Host testing: Audacity](../../develop/host-testing.md#audacity-vst3) |

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
