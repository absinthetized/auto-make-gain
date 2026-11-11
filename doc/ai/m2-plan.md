# M2 plan — supply-chain upgrade (branch series, 2026-11-11)

> Audience: AI/owner process material (ledger). Facts-only evidence;
> rationale = the owner rulings quoted. Successor of `m1-plan.md`.

## Owner rulings (2026-11-11, gates this milestone)

Between v0.2.0 and now: the mac toolchain bumped (Apple clang 21.0.0,
macOS 27.2) and the Asahi host bumped to **Fedora Asahi 45** (the
container is pinned to 44). Milestone: upgrade the supply chain so the
pinned toolchain matches the machines it serves again.

- **Order** (owner): Xcode rebase FIRST ("faster to test on macos"),
  then fedora 45, then JUCE 9.
- **JUCE 9 IN scope** (owner), unblocked by the compatibility news the
  owner asked about: clap-juce-extensions landed "JUCE 9 support
  (#178)" on JUCE 9 release day (2026-07-21) plus embedded-editor
  double-scale fix (#183); JUCE 9 native CLAP is NOT in the release
  (AudioProcessor v2 WIP), so the extension remains the CLAP path.
- **Queued later, wiped now** (owner): M3 = "more real-time
  compensation" (owner elaborates); M4 = CI/CD + release process
  (packaging, signing, audit tooling); all other M1-era candidates
  (noise gate FR §9, SIMD, stereo metering, M1 ⏳ smoke rows, fold
  hardening) are wiped by owner consent.

| PR | Branch | Content | Gate |
|---|---|---|---|
| M2a | `pr-m2a-xcode-rebase` | native mac lane on clang 21 / macOS 27.2; fresh configure+build+ctest; doc truth check | owner smoke |
| M2b | `pr-m2b-fedora45` | container pin 44→45 (`docker/fedora-dev.Dockerfile`), bootstrap dnf-list sanity vs 45, version-skew guard re-run (gcc container vs Asahi), arm64 + `--x64` lanes green, build.md §7/§8 truth | lanes + owner |
| M2c | `pr-m2c-juce9` | JUCE 8.0.15 → latest 9.0.x; clap-juce-extensions `7adee3a` → latest main; pins-table row for lunasvg (new JUCE dep) | both lanes + owner Reaper smoke with attention to FR-27 zoom (extension #183 touches editor scale) + Asahi Ardour/CLAP |

## M2a evidence (2026-11-11, Apple clang 21.0.0, macOS 27.2)

- Fresh configure (no stale cache): 230.8 s, exit 0.
- Build all formats: exit 0. Warnings: **3**, all JUCE-internal
  `MTLResourceStorageModeManaged`-family deprecations — the same
  situation build.md §6.2 already documents; nothing new from clang 21.
- ctest: 100% (1/1) in 0.71 s.
- `COPY_PLUGIN_AFTER_BUILD` auto-install verified byte-identical
  (`cmp`) against the freshly built VST3.
- No source-file changes needed — the rebase is pure verification +
  this record; docs truth (build.md "macOS 27" rows) already accurate.
