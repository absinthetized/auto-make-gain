# MakeGain (auto-make-gain)
my tentative to **vibe-code** an auto-gain DAW plugin — agentic
development in full: an AI agent plans, writes and comments the code
under a strict human-sized commit policy, while the human owner freezes
requirements, rules decisions, runs every machine checkpoint and merges.

**MakeGain** is a DAW plugin that keeps your processing chain's loudness
steady: train it on the clean signal, then let it automatically compensate
any gain change introduced by the plugins in front of it (FR
FR details: [the frozen FR contract](doc/human/MakeGain_FR-v1.md)).

- Framework: **JUCE** (C++), formats VST3 + CLAP + AU
  (see [the tech spec](doc/ai/MakeGain_TS-v0.1.md) for the decision record)
- Platforms: macOS arm64, Linux aarch64 (Fedora Asahi), Linux x86_64 (best-effort)
- Licence: **AGPLv3** — see [`LICENSE`](LICENSE)

## How it's verified
Every Linux compile/test cycle runs through committed container lanes
(agents may never touch `docker` directly): `scripts/docker-check.sh` for
the native **arm64** lane, `--x64` for the Rosetta-emulated **amd64**
lane (details: [`doc/human/build.md` §8](doc/human/build.md)). DSP code is JUCE-free
and unit-tested separately from the framework on both arches.

## Docs
- **New here / never used JUCE?** Start with [`doc/human/code-tour.md`](doc/human/code-tour.md) — where the entry point is and how the pieces move
- **Fresh machine?** `./scripts/bootstrap.sh` — checks/installs the build toolchain (details: [`doc/human/build.md`](doc/human/build.md); dependencies of record = that script + `build.md` §7)
- [Functional requirements](doc/human/MakeGain_FR-v1.md) — frozen contract (v1)
- [Technical specification](doc/ai/MakeGain_TS-v0.1.md) — draft v0.1, veto items in §9
- [Release model](doc/ai/release-model.md) — versions, tags (owner cuts them), CHANGELOG rules
- [Changelog](CHANGELOG.md) — one section per tag

