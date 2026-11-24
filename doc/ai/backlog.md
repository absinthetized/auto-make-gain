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
   Explicitly *not* started; **waits for the owner's elaboration brief**
   (scope lives in DSP/UI territory: latency, windowing, ramp behavior).
   No agent-side design until that brief exists.

2. **M4 — CI/CD + release process** — queued last (owner 2026-11-11):
   GitHub Actions matrix, packaging (signed macOS artifacts),
   dependency-audit tooling (syft/grype), and the rule-12 snapshot
   doc-lint automation (plus running the version gate on tag pushes).

## Open loose ends (not new work)

- Tier-2 Asahi session: ~~`gcc --version` skew-guard close-out~~ —
  **closed 2026-11-21** (16.2.1 both sides, host packaging newer; see
  m2-plan §M2b); ~~Ardour/CLAP load on the JUCE 9 binaries~~ —
  **closed 2026-11-23, fully green** (m2-plan §M2c). Nothing left on M2.
- Pen payload refresh: `~/Desktop/makegain-linux-arts-55f2909/` holds
  JUCE-9.0.5-verified aarch64 VST3 + CLAP + JSFX TAP scripts, copied
  from the M2c lane artifacts.
