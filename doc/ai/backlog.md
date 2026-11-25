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

## Queued items (in order)

1. **M3 — faster / more real-time compensation** — owner, 2026-11-21.
   **In progress** (started 2026-11-24 on `pr-m3a-fast-level`): scope,
   rulings, contract delta and the commit breakdown live in
   `m3-plan.md`. Two modes (Fast level / Smooth LUFS), a fixed 80 Hz
   floor in the Fast detector, and the Mode switch replacing the Metric
   and Response switches.

2. **M4 — CI/CD + release process** — queued last (owner 2026-11-11):
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

## Open loose ends (not new work)

- Tier-2 Asahi session: ~~`gcc --version` skew-guard close-out~~ —
  **closed 2026-11-21** (16.2.1 both sides, host packaging newer; see
  m2-plan §M2b); ~~Ardour/CLAP load on the JUCE 9 binaries~~ —
  **closed 2026-11-23, fully green** (m2-plan §M2c). Nothing left on M2.
- Pen payload refresh: `~/Desktop/makegain-linux-arts-55f2909/` holds
  JUCE-9.0.5-verified aarch64 VST3 + CLAP + JSFX TAP scripts, copied
  from the M2c lane artifacts.
- Fast vs Smooth on program material: the M3b acceptance is perceptual
  (2026-11-25); R-G1/R-G2 measure convergence on synthetic signals
  only. A rendered comparison would make the host verdict checkable.
