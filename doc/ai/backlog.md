# Backlog — queued work (owner-ordered)

> Ledger, not a snapshot: this file holds work that is *decided but not
> started*. Items leave when they land (record → plan file of their
> milestone, e.g. `m2-plan.md`) or when the owner drops them.

## Landed

- **Version surfaced in the plugin GUI** (was #1) — landed 2026-11-23 on
  `pr-version-in-gui`: CMake `project(VERSION)` = 0.2.1 as the single
  version truth, configure-time git-sha build stamp compiled in, GUI
  title-row stamp (`v0.2.1 / <sha>`), `scripts/check-version.sh` wired
  as tier-1 step `[0/2]`, `scripts/stage-linux-artifacts.sh` for stamped
  pen payloads, AGENTS rule 13 (Tier-2 artifact provenance).
- **M2 — toolchain maintenance (Fedora 45 + JUCE 9.0.5)** — merged as
  `Pr m2c juce9 (#14)`; both Docker lanes green. Owner gate closed
  2026-11-23 (below). Evidence: m2-plan §M2b/§M2c.
- **M3 — faster / more real-time compensation** — merged as
  `pr-m3c-ui-docs`, version 0.3.0: two compensation modes (Fast level /
  Smooth LUFS) behind one Mode control, the 80 Hz Fast band floor, the
  panel's band/span caption, and the session migration from the retired
  Metric/Response parameters. Bare-metal Asahi run accepted by the owner
  2026-11-26. Evidence and rulings: `m3-plan.md`.

## Queued items (in order)

1. **M4 — CI/CD + release process** — queued last (owner 2026-11-11):
   GitHub Actions matrix, packaging (signed macOS artifacts),
   dependency-audit tooling (syft/grype), and the rule-12 snapshot
   doc-lint automation (plus running the version gate on tag pushes).
   - **Compiler refinement + agentic context refinement** (owner,
     2026-11-25; refined 2026-11-25): one activity with two halves.
     - *Compiler side*: turn on `-Wall -Wextra` (then `-Werror` once
       clean) for the build targets. Origin: the C5 RED commit carried
       a dead static helper that built silently — the repo has no
       warning flags at all, so `-Werror` was never in play.
     - *Agentic context side*: the owner's reading is that too much
       build and test output is injected into agent sessions, which
       drives token counts and the bill up. Scope this half when the
       item is planned: what an agent reads from a build/test run by
       default, what it should get instead (failing lines, a summary,
       a file it can open on demand), and how the repo's scripts and
       instructions shape that.
     Treat as build/tooling hygiene, reviewed on its own; the owner
     judged it a separate activity from the M3 work, not a C5 fix-up.
   - **CI builds the plugin target** (origin `d9d5ae5`): M3a's C4
     commit added `src/dsp/fast_level.cpp` to the test target only, so
     the VST3/AU/CLAP bundles had not linked since that commit and
     nothing noticed until a manual host rebuild. CI must build
     `MakeGain_All`, not just `makegain_tests`; the two targets keep
     separate source lists, so the test lane cannot catch a missing
     plugin source.
     - **`MakeGain_All` does not include CLAP** (owner hit it,
       2026-11-25): `MakeGain_All` comes from `juce_add_plugin`
       (`FORMATS VST3 AU`), while the CLAP bundle is the separate
       `MakeGain_CLAP` target layered on by `clap_juce_extensions`.
       A `--target MakeGain_All` build leaves the installed CLAP
       bundle stale, so a host loaded an old binary that still showed
       the previous panel. A full default build (`cmake --build
       build/<preset>`, what `check-all.sh` runs) covers it. CI must
       build the default all-target, or `MakeGain_All` + `MakeGain_CLAP`
       explicitly.

2. **Three-stage workflow — Fast, then Smooth, then Freeze** (owner,
   2026-11-25). A use case discovered in real use, queued as a
   *candidate*, not an order:
   - build and adjust the chain in **Fast** (the reading moves in tens
     of milliseconds, so edits are audible immediately);
   - once the chain stabilises, switch to **Smooth** — slower, but the
     LUFS reading is the ear-accurate one;
   - when happy, **Freeze** once and leave it as a static trimmer.

   The sequence is already allowed (FR-28 switching while not frozen,
   FR-30 locking it while frozen), but the panel does not *suggest* it:
   Mode sits in a flank and Freeze in the bottom strip, so nothing reads
   as a progression. A layout pass that expresses the three stages is
   queued **if the workflow proves ingrained** (owner: wants to use it
   more before redesigning). Doc side is done: `usage.md` states the
   workflow as a recommended path.

## Open loose ends (not new work)

- Tier-2 Asahi session: ~~`gcc --version` skew-guard close-out~~ —
  **closed 2026-11-21** (16.2.1 both sides, host packaging newer; see
  m2-plan §M2b); ~~Ardour/CLAP load on the JUCE 9 binaries~~ —
  **closed 2026-11-23, fully green** (m2-plan §M2c). Nothing left on M2.
- Pen payload: `~/Desktop/makegain-linux-arts-b589b9b/` holds the 0.3.0
  aarch64 VST3 + CLAP + JSFX TAP scripts, staged from the M3c lane at
  that commit (stamped, sha-named, MANIFEST + SHA256SUMS).
- Fast vs Smooth on program material: the M3b acceptance is perceptual
  (2026-11-25); R-G1/R-G2 measure convergence on synthetic signals
  only. A rendered comparison would make the host verdict checkable.
