# Backlog — queued work (owner-ordered)

> Ledger, not a snapshot: this file holds work that is *decided but not
> started*. Items leave when they land (record → plan file of their
> milestone, e.g. `m2-plan.md`) or when the owner drops them.

## Queued items (in order)

1. **Version surfaced in the plugin GUI** — ruling 2026-11-21. Origin:
   `strings` check showed the binaries self-report **0.1.0** while the
   tag was v0.2.0 — `CMakeLists.txt` `project(VERSION 0.1.0)` is stale
   and `juce_add_plugin` has no VERSION override, so
   `JucePlugin_VersionString` never tracked the tag. Fix = CMake is the
   single version source; the GUI shows it.
   - Branch `pr-version-in-gui` off `main` (independent of M2 — no file
     overlap).
   - Commit decomposition (owner-approved):
     1. `build: project VERSION -> 0.2.1` (build script → eyeball gate)
     2. `ui: plugin version in the title row` — `JucePlugin_VersionString`
        drawn dim, top-right of the title row (source → eyeball gate).
   - **Version number: v0.2.1** (owner ruling).
   - **Placement: title-row right corner** (owner ruling; "status bar is
     already busy").
   - Note for the tag ceremony: bumping `project(VERSION ...)` becomes
     part of the owner's cut ritual from here on.

2. **M3 — faster / more real-time compensation** — owner, 2026-11-21.
   Explicitly *not* started; **waits for the owner's elaboration brief**
   (scope lives in DSP/UI territory: latency, windowing, ramp behavior).
   No agent-side design until that brief exists.

3. **M4 — CI/CD + release process** — queued last (owner 2026-11-11):
   GitHub Actions matrix, packaging (signed macOS artifacts),
   dependency-audit tooling (syft/grype), and the rule-12 snapshot
   doc-lint automation.

## Open loose ends (not new work)

- M2 merge: `pr-m2c-juce9` (JUCE 9.0.5 + extension `6bde108`, both lanes
  green) is pushed but unmerged as of 2026-11-21; `main` remains
  v0.2.0/JUCE 8 until it merges.
- Tier-2 Asahi session: `gcc --version` skew-guard close-out (M2b
  follow-up) and Ardour/CLAP load on the JUCE 9 binaries.
- Pen payload refresh: `~/Desktop/makegain-linux-arts-55f2909/` holds
  JUCE-9.0.5-verified aarch64 VST3 + CLAP + JSFX TAP scripts, copied
  from the M2c lane artifacts.
