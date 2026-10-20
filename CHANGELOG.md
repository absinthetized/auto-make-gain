# Changelog — MakeGain

One section per tag (`doc/ai/release-model.md`). Entries are written at
tag time by the owner; sections below are prepared in advance and frozen
at the tag commit.

## v0.1.0 — walking skeleton (tag pending, owner's cut)

The first tagged state. Honest boundary: the plugin **loads and passes
audio through unchanged**; the DSP machinery is built and heavily tested
but *not yet wired into the signal path* (that lands with the gain
computer, M1 PR-B). Host coverage: Reaper on macOS arm64; VST3 + CLAP.

### Added
- Plugin skeleton, JUCE 8.0.15: VST3 + CLAP from one shared code target;
  build-time copy into the user's plug-in folders.
- JUCE-free DSP layer (`src/dsp/`):
  - K-weighting cascade (DeMan per-sample-rate formulation), numerically
    anchored to ITU BS.1770-4 Table 1 (+0.691 dB at ~1 kHz — the constant
    the LUFS formula cancels).
  - Dual-metric loudness kernel: short-term LUFS + unweighted dB RMS off
    one 100 ms sub-block accumulation ring (drift bounded by one
    sub-block, not by session length).
- Editor with a self-verifying status line naming what the loaded binary
  actually contains.
- Verification harness: Catch2 v3 golden tests, sanitizer presets
  (ASan/UBSan/LSan), `scripts/check-all.sh` as the CI stand-in.
- Tiered Linux verification: `scripts/docker-check.sh` — arm64 native
  (every commit) and `--x64` Rosetta-emulated amd64 (pre-PR) lanes; the
  fedora:44 image mirrors the Asahi Remix userland exactly.

### Validated
- All DSP assertions green on macOS arm64, Linux aarch64 (native) and
  Linux amd64 (Rosetta emulation, incl. sanitizers); gcc 16.2.1 both
  Linux arches; Reaper passthrough load-checks repeated milestone-round.
- Still open at this tag: AU exercise in GarageBand, Ardour+CLAP host
  check, native DSP behavior (arrives with the wiring).
