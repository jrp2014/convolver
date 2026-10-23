<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Maintaining the documentation

[Documentation index](../README.md) · [Implementation map](architecture.md)

For anyone, person or coding agent, who writes or changes documentation.

**In short:**

- Every maintained page is for one reader and is one kind of page. Readers have
  their own folders: `docs/use/` for people applying filters, `docs/develop/`
  for people building or changing Convolver, `docs/project/` for status, plans
  and decisions. Dated records live in `docs/archive/` and are never edited.
- The four kinds are tutorial, how-to, reference and background. A section
  written for another reader or kind moves to its own page and leaves a link.
- Each fact has one home (table below); other pages link to it.
- Developer pages double as agent instructions, so repository skills and
  `AGENTS.md` point at them instead of repeating them.

The move into reader folders is under way (DOC-4, [plan](../project/plans/2026-10-23-documentation-by-reader.md)):
all three folders exist; the last step shortens the front pages and checks that
every maintained page sits in a reader folder.

Contents: [readers](#readers-and-folders) · [kinds of page](#kinds-of-page) ·
[writing rules](#writing-rules) · [pages agents follow](#pages-agents-follow) ·
[one home for each topic](#one-home-for-each-topic) ·
[updating a behaviour](#updating-a-behavior) ·
[current claims and dated evidence](#current-claims-and-dated-evidence) ·
[automated checks](#automated-checks)

## Readers and folders

| Folder | Reader | What they want | Keep out |
| --- | --- | --- | --- |
| `docs/use/` | Someone applying filters with the command-line tool, in a plug-in host, or system-wide on Windows. Shipped in preview packages. | Install, load a filter, hear it, fix a problem | Build recipes, SDK pins, internals, TODO IDs, test evidence |
| `docs/develop/` | Someone building from source, embedding the library, packaging a fork or changing Convolver, including coding agents | Build, find the code, know the rules and contracts, prove a change | User hand-holding, history |
| `docs/project/` | jrp, and anyone asking where Convolver is going and why | Status, direction, open plans, decisions, history | Instructions |
| `docs/archive/` | Anyone checking what was true or decided at a date | Dated reviews, completed plans, release records | Anything current; records are never edited, but the index pages in it (such as `archive/plans/README.md`) are maintained |

The root keeps the files people and GitHub expect: `README.md`,
`CONTRIBUTING.md`, `AGENTS.md`, `CLAUDE.md`, `TODO.md` and `CHANGELOG.md`.

## Kinds of page

| Kind | Purpose | Shape |
| --- | --- | --- |
| **Tutorial** | Get a newcomer to a first success | One path, real example files, the expected result at each step, no options |
| **How-to** | Do one task | Prerequisites, numbered steps, how to tell it worked, what to do if it did not |
| **Reference** | Look something up | Complete; tables; one topic per heading; no story and no history |
| **Background** | Understand why or how | Prose and diagrams, no steps; links to the how-to and reference pages |

The [index](../README.md) shows each page's kind next to its title. Dated
records (reviews, release records, completed plans) are not one of the four
kinds; the index labels them **Record**.

## Writing rules

Write for a reader who has not seen the development conversations. Short
pages help only if each sentence can be understood on its own.

1. Open with one line saying who the page is for and what they will be able to
   do, then an **In short** summary in full sentences. Longer pages add a
   contents line. When a change alters a summarized fact, update the summary too.
2. Cover one reader and one kind. If a section is for someone else, move it and
   leave a one-line link.
3. Define a term the first time it is used, or link to [concepts](../use/concepts.md).
   Explain what an option does before its internal mechanism, and use one
   working example before describing exceptions.
4. Say each fact in one place; other pages link to it, so they cannot drift
   apart.
5. Use headings a reader would search for: tasks on how-to pages ("Load a
   filter in your host"), nouns on reference pages ("Output formats"). Keep link
   text meaningful and tables small enough to scan.
6. Call the project's owner jrp, and give dates as "23 October 2026".

On pages in `docs/use/`:

- No TODO IDs, pull request numbers, commit hashes or decision dates in the
  body. Where something is unfinished, one **Status** note near the top says so
  in plain words and links to the status page.
- No internal names (`HostFilter`, "generation", "oracle") or project words
  ("adapter", "candidate") without an explanation.
- State behaviour, not evidence: "the plug-in plays silence if the preset does
  not match" belongs here; which test checks it belongs in [testing](testing.md).
- End each how-to with how to tell it worked and where to look if it did not.
- Aim for fewer than about 150 lines; a longer page is usually two tasks.

On pages in `docs/develop/`, TODO IDs, source paths and test names are welcome,
because this reader acts on them. Contracts (what must stay true) live in
reference pages such as the [plug-in adapter contract](plugin-contract.md), not
in plans. Records carry their date and commit and are never edited afterwards;
a newer page supersedes them.

## Pages agents follow

Coding agents read the same pages as people. Write `docs/develop/` so an agent
can act on it without a separate copy of the rules:

- Give commands exactly, in code blocks, from the repository root, with the
  expected result or the check that shows success.
- Write procedures as numbered steps with explicit conditions ("if the pull
  request already exists, …"), and checklists as lists an agent can tick.
- Name files, targets, tests and options exactly, in backticks, so they can be
  searched for. `scripts/check_repository.py` checks that paths named in
  `AGENTS.md`, `CONTRIBUTING.md`, the [implementation map](architecture.md) and
  [workflow](workflow.md) exist.
- Keep anchors stable; skills and `AGENTS.md` link to sections by anchor.
- Put every rule on exactly one page. `AGENTS.md` lists the rules that apply
  everywhere and links to the rest; a repository skill in `.claude/skills/`
  only says when it applies and which section to follow. If a skill needs
  more than that, the missing text belongs on the page.

## One home for each topic

| Topic | Authoritative page | Check against |
| --- | --- | --- |
| Purpose and first command | [README](../../README.md) | Build and run the example |
| Navigation and vocabulary | [Index](../README.md), [using Convolver](../use/README.md), [concepts](../use/concepts.md) | Current entry points; avoid unexplained abbreviations |
| First successes | [Your first filter](../use/first-filter.md), [your first sound in a host](../use/first-sound-in-a-host.md) | `examples.first_use` runs the first; the release examples check the second |
| Configuration grammar | [Configuration](../use/configuration.md) | `config_parse.cpp`, `config.cpp`, corpus |
| CLI options, output formats and inspection | [Command-line reference](../use/command-line.md) | `main.cpp`, `inspection_cli.cpp` and tests |
| What Convolver does not do | [Limits](../use/limits.md) | TODO's documented limitations |
| Symptoms and fixes | [Troubleshooting](../use/troubleshooting.md) | Diagnostics in `config.cpp`, the tools and the adapters |
| Other readers of the format, path portability | [Other programs](../use/other-programs.md) | `tests/compatibility/`, `downstream-evidence.json` |
| Component boundaries and change map | [Implementation map](architecture.md) | Actual symbols, callers, targets and test names |
| Package consumption, engine API and resource limits | [Library guide](library.md) | CMake exports, public headers, external-consumer tests, `native.limits` |
| Building from source, every target and format, CMake presets | [Build from source](build.md) | `CMakeLists.txt`, `CMakePresets.json`, the CI workflows |
| Installing the plug-ins | [Install](../use/install.md) | Packaging, installers, each format's bundle layout |
| How the plug-ins behave for users | [How the plug-ins behave](../use/plugins.md) | The adapter contract, adapter and harness tests |
| What every plug-in adapter must do | [Plug-in adapter contract](plugin-contract.md) | `host.hpp`, the PLUG-2 harness, each adapter's tests |
| Plug-in identities and channel layouts | [Plug-in identities](plugin-identities.md) | `plugins/identities.json`, `tests/identities_tests.py`, `vst3.identities` |
| Plug-in presets and the preset tool | [Plug-in presets](../use/presets.md); file formats in the [contract](plugin-contract.md#preset-files) | `portable/presets.*`, `portable/preset_main.cpp`, `native.presets`, `plugins.presets`, `vst3.preset_format` |
| System-wide filtering on Windows | [Equalizer APO export](../use/equalizer-apo.md) | `portable/equalizer_apo*.cpp`, `export.equalizer_apo` and its model of Equalizer APO |
| Plug-in downloads and release gates | [Install](../use/install.md), [releasing](releasing.md), [build from source](build.md#pinned-preview-recipe), [host testing](host-testing.md) | `packaging/release-policy.json`, `scripts/release/`, `tests/release/`, candidate workflow |
| Status, evidence levels and direction | [Status](../project/status.md) | TODO, open pull requests, GitHub releases, host records |
| jrp's decisions | [Decisions](../project/decisions.md), with the details on the page each row links to | "Owner's decision" lines in TODO and the guides |
| Dependency choices | [Implementation map](architecture.md#dependency-choices) | `CMakeLists.txt`, the release policy |
| FFT selection and performance evidence | [FFT guide](fft-backends.md) | Adapter contract; dated measurement artifacts |
| Verification | [Testing](testing.md) | CMake registrations, workflow matrix, actual run results |
| Open work | [TODO](../../TODO.md) | Current source and linked issues/plans |
| Claiming, handing over and reviewing work | [Workflow](workflow.md) | Pull request template, repository checks, `.claude/skills/` (pointers only) |
| How documentation is organised and written | This page | The index, the link and repository checks |
| Past releases and provenance | [History](../project/history.md) and dated records | Recorded evidence; never rewrite a past result as current |

Short summaries may link to these pages; avoid copying their option lists,
limits, test counts or TODOs into several places. Preview packages ship every
page in `docs/use/`; `scripts/release/package_docs.py` turns their links to
other pages into links to the source repository at the packaged commit, so
write those pages as usual. Commands marked `<!-- executable-example -->` in
`docs/use/` must run a bundled tool as `./bin/TOOL INPUT OUTPUT...` from the
package folder; the release tests run them. `VERSION` is the numeric source of
truth. Version numbers in commands are examples or compatibility requirements,
not another release metadata source.

## Updating a behavior

For contributors and agents:

1. Identify the source symbol, its callers and tests using the implementation map.
2. Update the owning guide in the same change. State the observable behavior,
   defaults, failure behavior and any compatibility effect.
3. Update navigation only when the reader's route through the docs changes.
   Keep existing heading anchors where possible; update inbound links if renamed.
4. Run documented examples where applicable. Distinguish commands actually run
   from platform instructions checked against CI configuration.
5. Run lint and local-link checks from [CONTRIBUTING](../../CONTRIBUTING.md). A link
   check establishes that a target exists, not that the prose agrees with code.
6. Put new work in TODO with a stable ID and completion evidence. Close it when
   implemented; do not leave completed features described as future proposals.

## Current claims and dated evidence

A source map can state a review date and commit. A test result needs its actual
revision, configuration and outcome. Benchmark results additionally need machine,
compiler, dependency versions, planner mode and workload. Avoid words such as
"latest", "all platforms" and "fully tested" without a defined scope.

Keep the historical website under `docs/historical/` byte-identical. Mark dated
reviews and plans as records rather than updating their results in place. Plans
in [`project/plans/`](../project/plans/) are design records for open work, not special authority
over current code or user instructions. (Until 21 October 2026 they lived in
`docs/superpowers/`, a name taken from an agent tool's convention.) When the code on
`main` implements all of a plan, move it to [`archive/plans/`](../archive/plans/README.md)
with an archive notice and a row in that index, and fix links to it. Put source
comments near non-obvious invariants; put contributor navigation here, rather
than adding large instruction blocks to implementation files.

External references should point to primary specifications, project repositories
or upstream documentation. Record the exact release/hash when adopting code.
An upstream link alone is not a licence audit, performance result or proof that
our vendored copy is current.

## Automated checks

The pre-commit configuration used by lint CI (which also checks C++ formatting) runs
[`check_doc_links.py`](../../scripts/check_doc_links.py) and its
[regression tests](../../tests/documentation_links_tests.py). The checker scans
top-level `.md` files and Markdown under `docs/`, excluding `archive/`,
`historical/` and `review-2026-10/` as source pages. Open plans in `project/plans/` are
checked like maintained guides. Links to the excluded
records are still checked. This keeps preserved evidence separate from maintained
instructions without accepting broken navigation into the evidence.

The check covers inline and reference-definition destinations, ATX headings
(`# Heading`, including duplicate headings) and HTML `id`/`name` anchors. Code
fences and inline code are ignored. It is a deliberately small, offline check,
not a complete Markdown renderer: it does not validate external URLs, detect
undefined reference labels, or verify commands and source claims. Review those
separately; extend the checker with a regression test if maintained guides need
additional Markdown syntax. DOC-2 in [TODO](../../TODO.md) tracks remaining automation.
