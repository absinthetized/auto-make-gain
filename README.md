# MakeGain (auto-make-gain)

**MakeGain** is a DAW plugin (VST3 + CLAP + AU, JUCE/C++) that keeps your
processing chain's loudness steady: it listens for the gain differences a
plugin chain introduces and compensates them automatically, so A/B
comparisons and level matching stay honest.

*License: **AGPLv3*** — see [`LICENSE`](LICENSE).
Platforms: macOS arm64, Linux aarch64 (Fedora Asahi), Linux x86_64
(best-effort).

> This repository is also an exercise in **agentic development**: the code
> is written and documented by an AI agent under a strict human-sized
> commit policy, while the human owner freezes requirements, rules every
> decision and merges. That process material lives in `doc/ai/` and
> [`AGENTS.md`](AGENTS.md); the docs below describe the *plugin as it is
> now*, not the journey.

## Documentation (read top to bottom as far as your goal takes you)

| Level | I want to… | Go to |
|---|---|---|
| 1 | **use the plugin** in my DAW — load it, route it, DAW-specific quirks | [`doc/human/usage.md`](doc/human/usage.md) |
| 2 | **install** a build I received as a bundle (unsigned) or onto a bare machine | [`doc/human/install.md`](doc/human/install.md) |
| 3 | **build it from source** on a fresh machine | [`doc/human/build.md`](doc/human/build.md) |
| 4 | **contribute** — assumptions, architecture, the working rhythm | [`doc/human/contributing.md`](doc/human/contributing.md) |
| 5 | **read the internals** — a framework-first tour of the code (new to JUCE?) | [`doc/human/code-tour.md`](doc/human/code-tour.md) |
| — | know what it must do — the frozen behavioral contract | [`doc/human/MakeGain_FR-v1.md`](doc/human/MakeGain_FR-v1.md) |

Agent/owner process material (plans, decision registers, the visual
grammar, release rules): [`doc/ai/`](doc/ai/) — append-only ledgers by
design; `doc/human/` above is always the current-state snapshot.

See [`CHANGELOG.md`](CHANGELOG.md) for what changed per release.
