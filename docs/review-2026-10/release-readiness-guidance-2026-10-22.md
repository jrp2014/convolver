<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Release-readiness guidance: 22 October 2026

jrp gave this guidance on the way forward on 22 October 2026, with `main` at
`63562ff` (0.4.19). It is kept here word for word as a dated record. Where each
recommendation is now tracked:

| Recommendation | Tracked as |
| --- | --- |
| 1. Define the first release by complete user journeys, followed by an independent tester | REL-2 (journeys), REL-3 (independent walkthrough) in [TODO](../../TODO.md#user-experience-and-distribution) |
| 2. Test lifecycle sequences with a small reference model | PLUG-7 |
| 3. Treat diagnostics as part of the adapter contract | PLUG-8, and the [shared adapter contract](../plans/2026-10-21-plugin-formats.md#failure-reasons); DIAG-1 then decides presentation |
| 4. Protect compatibility using artifacts from actual releases | COMPAT-1, and the guarantees table in [release readiness](../release-readiness.md#compatibility-guarantees) |
| 5. Establish resource budgets before further optimisation | PERF-3 |
| 6. Keep abstraction and dependency growth tied to demonstrated need | The [execution priorities](../../TODO.md#execution-priorities) and [modernization direction](../modernization.md#priorities) |
| 7. Make project status cheaper to understand | [Release readiness](../release-readiness.md) |
| Next milestone | [Release readiness](../release-readiness.md#next-milestone-an-independent-user-runs-a-preview) and the [execution priorities](../../TODO.md#execution-priorities) |

Later changes to the plans belong in TODO and the maintained guides, not here.

## The guidance

> Yes. From a senior developer’s perspective, I would put more emphasis on
> finishing a dependable product and controlling its long-term maintenance
> cost. The architecture is sound enough to support that; the next decisions
> matter more than another substantial refactor.
>
> My additional recommendations, in priority order:
>
> 1. Define the first release by complete user journeys.
> Choose a few concrete examples: stereo room correction, a 2→6 crossover, and
> offline file rendering. For each advertised platform, someone unfamiliar with
> the repository should be able to download, install, load a filter, obtain the
> expected output, save and reopen, and recover from a wrong preset using the
> documentation alone. Have an independent tester follow those instructions.
> This gives “ready” a more useful meaning than a collection of completed TODO
> items.
> 2. Test lifecycle sequences, not just individual operations.
> The reset defect is revealing: reset, replacement and initial loading each
> have tests, but their interaction still fails. Add deterministic sequences
> involving load, process, reset, bypass, failed replacement and restart. Check
> invariants after each operation: which filter is saved, which is audible,
> whether silence is required, and whether callback constraints hold. Extend
> this to outstanding asynchronous loads when LV2 arrives. A small reference
> model would be useful; a large state-machine framework would add unnecessary
> machinery.
> 3. Treat diagnostics as part of the adapter contract.
> Silence can be correct processing behavior and still be a confusing user
> experience. Preserve a structured failure reason and its associated filter
> request through every adapter, even while the eventual display mechanism
> remains deferred. Keep diagnostic state off the audio thread. When the
> planned diagnostics review happens, it should mostly decide how to present
> information already available, rather than reconstruct information discarded
> earlier.
> 4. Protect compatibility using artifacts from actual releases.
> The existing literal version-1 state fixture is a good start. Add a small
> retained collection of presets and host projects produced by released builds,
> together with expected audio. Test upgrades against those artifacts.
> Round-trip tests using today’s writer and reader can agree with each other
> while both breaking yesterday’s files. Keep the guarantees for configuration
> syntax, serialized state, plug-in identity and the C++ API distinct.
> 5. Establish resource budgets before further optimisation.
> Measure callback duration distributions, memory consumption and
> filter-preparation time for representative long filters and six/eight-channel
> routing. Include replacement, when two engines can run together, and reset,
> whose work scales with stored history. Use those results to define supported
> workloads. This will tell you whether partitioning, memory layout or another
> FFT implementation deserves attention. Average throughput alone cannot answer
> that question.
> 6. Keep abstraction and dependency growth tied to demonstrated need.
> Continue the parallel AU/LV2 work, but let their implementations reveal what
> genuinely belongs in shared code. `HostFilter` is a useful extraction because
> the behavior already exists in VST3 and must agree across formats. Avoid
> generalising host lifecycle or release machinery far beyond those concrete
> requirements. Keep libsndfile at the I/O boundary and C++20 in the core; both
> remain appropriate choices.
> 7. Make project status cheaper to understand.
> Keep TODO as the backlog, but maintain a compact release-readiness view: what
> users can run now, what remains unqualified, the next release blockers, and
> their evidence. Distinguish “implemented,” “automatically tested,” “tested in
> a named host,” and “published.” Preserve dated reviews as records rather than
> repeatedly updating their historical conclusions.
>
> I would make the next milestone: an independent user successfully installs
> and uses an exact preview package, with the lifecycle regression fixed and
> the advertised workflows demonstrated. That would convert the considerable
> engineering progress into evidence that Convolver is ready for others to
> enjoy.
