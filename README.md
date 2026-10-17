# MakeGain (auto-make-gain)
my tentative to ai-in-the-loop code an auto-gain plugin that runs natively on linux aarch64 (ashai)

**MakeGain** is a DAW plugin that keeps your processing chain's loudness
steady: train it on the clean signal, then let it automatically compensate
any gain change introduced by the plugins in front of it (FR
details: [`functional_reqs/`](functional_reqs/MakeGain_FR-v1.md)).

- Framework: **JUCE** (C++), formats VST3 + CLAP + AU
  (see [`tech_specs/`](tech_specs/MakeGain_TS-v0.1.md) for the decision record)
- Platforms: macOS arm64, Linux aarch64 (Asahi), Linux x86_64 (best-effort)
- Licence: **AGPLv3** — see [`LICENSE`](LICENSE)

## Docs
- **Fresh machine?** `./scripts/bootstrap.sh` — checks/installs the build toolchain (details: [`docs/m0-plan.md`](docs/m0-plan.md))
- [Functional requirements](functional_reqs/MakeGain_FR-v1.md) — frozen contract (v1)
- [Technical specification](tech_specs/MakeGain_TS-v0.1.md) — draft v0.1, veto items in §9
