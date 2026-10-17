# MakeGain (auto-make-gain)
my tentative to **vibe-code** an auto-gain DAW plugin — agentic
development in full: an AI agent plans, writes and comments the code
under a strict human-sized commit policy, while the human owner freezes
requirements, rules decisions, runs every machine checkpoint and merges.
Runs natively on linux aarch64 (ashai).

**MakeGain** is a DAW plugin that keeps your processing chain's loudness
steady: train it on the clean signal, then let it automatically compensate
any gain change introduced by the plugins in front of it (FR
details: [`functional_reqs/`](functional_reqs/MakeGain_FR-v1.md)).

- Framework: **JUCE** (C++), formats VST3 + CLAP + AU
  (see [`tech_specs/`](tech_specs/MakeGain_TS-v0.1.md) for the decision record)
- Platforms: macOS arm64, Linux aarch64 (Asahi), Linux x86_64 (best-effort)
- Licence: **AGPLv3** — see [`LICENSE`](LICENSE)

## Docs
- **New here / never used JUCE?** Start with [`docs/code-tour.md`](docs/code-tour.md) — where the entry point is and how the pieces move
- **Fresh machine?** `./scripts/bootstrap.sh` — checks/installs the build toolchain (details: [`docs/build.md`](docs/build.md))
- [Functional requirements](functional_reqs/MakeGain_FR-v1.md) — frozen contract (v1)
- [Technical specification](tech_specs/MakeGain_TS-v0.1.md) — draft v0.1, veto items in §9
