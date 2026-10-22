# Decision register

> **Audience: AI working material — and the owner's memory.** Flat ledger
> of every owner ruling, newest first. One row per decision: date · ruling
> · where the operative text lives. Why this file: agents re-read one
> small file instead of hunting provenance across the whole doc estate
> (context compaction), and human-facing docs stay free of process
> narration. Rules: append a row **at decision time**; never rewrite
> history — a superseded decision gets a new row that names the old one;
> operative text wins over this ledger if they ever disagree.

| Date | Decision (one line) | Operative text lives in |
|---|---|---|
| 2026-10-22 | **Latency tuning postponed**: feel of the compensation (ramp 300 ms / 3 s window) is accepted as-is for M1; the PR-B3 Ramp parameter makes it tunable and defaults get revisited only once heuristics exist | m1-plan PR-B3 scope; `dsp::GainComputer` constants |
| 2026-10-22 | **mac smoke workflow**: the AGENT builds + installs the macOS binaries (VST3/CLAP/AU, Release) and the owner only tests — the PB2c-1-era "owner builds his own mac binaries" convention is retired | build.md §2 (`COPY_PLUGIN_AFTER_BUILD` auto-install; CLAP folder copy verified manually) |
| 2026-10-22 | **Amendment A2**: internal-sidechain AGC adopted — one plugin, two stereo input pairs (wet main + dry aux), continuous gain = −delta(wet, dry), no training; aux bus mirrors the main's channel count (mono tracks get a mono aux on pin 2); Reaper JSFX TAP (stereo + mono variants) is optional over plain sidechain routing; SRC/DST two-instance pair over IPC evaluated and **rejected** | `MakeGain_FR-v1.md` §10; m1-plan re-scope (PR-B2/B3) |
| 2026-10-22 | Host matrix: **GarageBand dropped** (AU exercise gate retires); AU build kept best-effort/unvalidated (same standing as the x86_64 lane); MaxComp default lowered 24 → 16 dB | `MakeGain_FR-v1.md` §10.5; m1-plan gates |
| 2026-10-20 | PB1 (gain machine) commits landed on `main` via the owner's push after the agent worked on an unverified checked-out branch — **accepted as-is, no history rewrite, no retroactive PR**; formalized: verify the checked-out branch before every commit and at session start | AGENTS.md rule 6 (amendment); the branch `m1-wire-up` PR description names the residue |
| 2026-10-20 | Docs estate reorganized: `doc/human/` vs `doc/ai/`, README navigation table, audience banners; humans review agent work in their editor (no diffs pasted in chat) | `README.md` docs map; AGENTS.md (cadence rules) |
| 2026-10-20 | Docs ceremony commits sliced onto `m1-docs-reorg` PR so PR-B stays pure code | this branch's history |
| 2026-10-19 | Ceremonies leaned: tags owner-only (agents never create/push, not even locally); CHANGELOG written at tag time; code-tour updated at milestone merges, not per-PR; **no** in-repo dependencies.md — dependencies of record = build.md §7 + `bootstrap.sh` | AGENTS.md rules 2+7; CHANGELOG.md header; build.md §7 |
| 2026-10-19 | Sanitizer runtimes (`libasan`/`libubsan`) explicit in `bootstrap.sh` — lane-critical packages are never by dependency luck (x86_64 link proved it) | `scripts/bootstrap.sh` (comment + package list) |
| 2026-10-19 | amd64 coverage closes via the `--x64` Rosetta lane (own PR during M1); its claims stop at codegen/libm evidence, never real-amd64-host claims; both lanes ALL GREEN (first pass) | build.md §8.2; m1-plan evidence record |
| 2026-10-19 | Distro lanes (Debian stable/Arch) evaluated and **dismissed** — revisit when CI exists; Debian-stable-first if revived | m1-plan ("Dismissed", future improvement) |
| 2026-10-18 | Commit cadence: per-commit owner eyeball; batch "ok"s cover the plan, never the commits; PR metrics ≤10 commits and ≤5–7 source files (warnings, not strict) | AGENTS.md rule 4 |
| 2026-10-18 | Red/green discipline: no implementation before its tests; RED = spec + honest failing stub (output quoted), GREEN = real implementation; owner reviews assertions pre-implementation | AGENTS.md rule 8 |
| 2026-10-18 | Docker containment: agents run Linux build/test containers **only** through committed repo scripts, never `docker` directly; on failure report verbatim, system fixes are the owner's | AGENTS.md rule 9 |
| 2026-10-18 | Two-tier Linux loop: Docker on the mac for every-commit compile/test; bare-metal Asahi is the Tier-2 oracle (bootstrap, DAW load, audio); stock-fedora container is faithful to the Remix userland; pen-transfer lane with gcc-version guard + sha-named artifact folders | build.md §8, §8.1 |
| 2026-10-18 | K-weighting formulation: DeMan per-sample-rate derivation (not RBJ shape); LUFS anchor −0.691 constant cancels the chain's real +0.691 dB gain → golden B1 = −3.0 LUFS for FS 997 Hz mono sine (owner's −3.7 proposal disproved by Table-1 evaluation); dB conversions of amplitude ratios use 20·log10 | test B1 comment (`tests/dsp/`); m1-plan architecture |
| 2026-10-18 | db-side M1 split: PR-A (kernels, red/green) then PR-B (gain compute + wiring + UI), chosen by the file metric | m1-plan "Two-PR milestone split" |
| 2026-10-17 | Commit policy: atomic, human-sized, plan-before-code, never mix unrelated; commit messages = what+why | AGENTS.md rule 4 |
| 2026-10-17 | Release & branching model: work unit = branch; agents never push/merge; semver, one tag scheme, no milestone tags; v0.1.0 = walking skeleton, tagged by the owner only | `doc/ai/release-model.md` |
| 2026-10-17 | Comment standard: every non-obvious call draws a "what this means in framework terms"; entry-point chain documented in-place + tour; no trivia, no rot-prone narration | AGENTS.md rule 7; `doc/human/code-tour.md` |

## Superseded / retired rulings

| Date | Retired | Superseded by |
|---|---|---|
| 2026-10-22 | Owner's interim ruling "B" — engagement-frozen makeup delta captured at each Activate (never committed; red planned, not written) — retracted the same day after the owner described the internal-sidechain delta model | 2026-10-22 Amendment A2 (`MakeGain_FR-v1.md` §10) |
| 2026-10-17 | code-tour "current whenever the file map changes" | 2026-10-19 lean (milestone cadence) |
| 2026-10-17 | "docs/ + functional_reqs/ + tech_specs/ layout" | 2026-10-20 `doc/` single-tree split |
