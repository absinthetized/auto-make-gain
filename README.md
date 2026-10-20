# MakeGain (auto-make-gain)
my tentative to **vibe-code** an auto-gain DAW plugin — agentic
development in full: an AI agent plans, writes and comments the code
under a strict human-sized commit policy, while the human owner freezes
requirements, rules decisions, runs every machine checkpoint and merges.

**MakeGain** is a DAW plugin that keeps your processing chain's loudness
steady: train it on the clean signal, then let it automatically compensate
any gain change introduced by the plugins in front of it
(behavioral contract: [the frozen FR](doc/human/MakeGain_FR-v1.md)).

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

## Where to go (docs map)

The docs are split by audience: `doc/human/` for people building or
reading the plugin, `doc/ai/` for the agent/owner process material
(plans, decision records, release rules).

| I want to… | Go to |
|---|---|
| **build it** on a fresh machine | [`doc/human/build.md`](doc/human/build.md) |
| **understand the code** (new to JUCE?) | [`doc/human/code-tour.md`](doc/human/code-tour.md) |
| **know what it does** — the behavioral contract | [`doc/human/MakeGain_FR-v1.md`](doc/human/MakeGain_FR-v1.md) |
| **know why it's built this way** — decisions & spec | [`doc/ai/MakeGain_TS-v0.1.md`](doc/ai/MakeGain_TS-v0.1.md) |
| release / tags / CHANGELOG rules | [`doc/ai/release-model.md`](doc/ai/release-model.md) |
| work on it **as an AI agent** | [`AGENTS.md`](AGENTS.md) (root — tooling convention) |
| **know every ruling to date** (owner decisions register) | [`doc/ai/decisions.md`](doc/ai/decisions.md) |
| see **what changed** per release | [`CHANGELOG.md`](CHANGELOG.md) |

