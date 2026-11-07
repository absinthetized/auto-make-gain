# Changelog — MakeGain

One section per tag (`doc/ai/release-model.md`). Entries are written at
tag time by the owner; sections below are prepared in advance and frozen
at the tag commit. Entries carry only facts (what appears, changes,
disappears for the *user*) — rationale and process live in the ledgers
(`doc/ai/`), not here.

## v0.1.0 — walking skeleton (tag pending, owner's cut)

### Added

- Plugin builds and loads in hosts: VST3 + CLAP from one shared code
  target (JUCE 8.0.15); automatic install into the user's plug-in
  folders on macOS.
- Scripts: `scripts/bootstrap.sh` (fresh-machine setup),
  `scripts/check-all.sh` (build + test), `scripts/docker-check.sh`
  (Linux verification lanes, arm64 + amd64) — see
  [`doc/human/build.md`](doc/human/build.md).
- Verification presets: Catch2 v3 golden tests for the DSP layer,
  sanitizer presets (ASan/UBSan/LSan).
- Editor window with a status line naming what the loaded binary
  contains.
- JUCE-free DSP sources (K-weighting, loudness metering) present in
  the tree but **not wired into the signal path** — audio passes
  through unchanged.
