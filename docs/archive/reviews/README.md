# Archived review documents

These assessments were superseded on 18 October 2026 by the
[current repository review](../../../REVIEW.md), based on portable 0.2.0 at
`5df874e`. They preserve how the project reached its present state.

| Record | Original scope | Successor |
| --- | --- | --- |
| [4.3 local review](4.3-local-review.md) | SourceForge 4.3 archive and a separate local patched source snapshot | [Current correctness review](../../../REVIEW.md#correctness-and-compatibility) |
| [4.3 branch review](4.3-branch-review.md) | Python model of 4.3 and initial review-branch fixes | [Current review entry point](../../review-2026-10/convolver-review.md) |
| [Original modernization assessment](2026-10-17-modernization.md) | Initial architecture/options assessment at `b3b479f`, with incremental early-port updates | [Current modernization priorities](../../modernization.md) |
| [Early portable structure review](early-portable-structure.md) | FFT abstraction and packaging milestone, with later partial updates | [Current structure review](../../project-structure.md) |
| [Documentation review, 20 October 2026](2026-10-20-documentation-review.md) | Reorganized the maintained guides at `2fe503e` (0.4.0); pre-review copies are in [documentation-2026-10-20](../documentation-2026-10-20/README.md) | [Documentation maintenance](../../documentation.md) |

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
