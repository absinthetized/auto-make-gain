# Changelog — MakeGain

One section per tag (`doc/ai/release-model.md`). Entries are drafted
in advance by the agent; the owner validates them (strikes, retunes)
and freezes the section at the tag commit. Entries carry only facts
(what appears, changes, disappears for the *user*) — rationale and
process live in the ledgers (`doc/ai/`), not here.

## Unreleased

### Added

- Automatic gain compensation, wired end to end: continuous chain-delta
  metering (short-term LUFS or unweighted dB RMS), per-sample ramped
  applied gain, big readout on the panel.
- Internal sidechain: one instance reads a dry pre-chain aux tap
  alongside the wet main (aux bus mirrors the main's channel count —
  stereo mains: pins 3/4, mono tracks: pin 2); Reaper TAP JSFX scripts
  ship with the repo (`jsfx/`).
- Parameters: **Active** (the one host-automatable control), **Metric**
  (LUFS / dB RMS), **Response** (Short 3 s / Fast 0.4 s), **MaxComp**
  (0–48 dB), **Ramp tc** (0.01–3 s).
- **Freeze**: holds the applied gain at its current value (static
  lock); disables the gain controls and locks Meter HP while held.
- **Meter HP**: high-pass on the metering only (20–400 Hz, default
  150 Hz), so low rumble is not metered as chain delta.
- Sidechain fault detection: when the aux input carries no signal the
  status band turns **red**.
- VST3 + CLAP + AU built from one shared code target; CLAP via
  clap-juce-extensions.
- **Panel zoom** (FR-27): manual dropdown, 75/100/125/150/200 %,
  default 100 %, persistent per session; no auto-DPI guessing.
- Documentation funnel (rule 12): `usage.md` (routing + panel manual),
  `install.md` (unsigned-bundle install), root `CONTRIBUTING.md`,
  snapshot-only `build.md`; `doc/ai/` stays the append-only ledger
  split.

### Changed

- Panel UI polish (PR-D): controls skinned by control *nature* —
  modality pills with caption-below for the two choice switches,
  on/off tick boxes for the booleans; status band color grammar
  (gray = standby, green = compensating, red = fault, ice = frozen);
  dependent controls grey out honestly; the automatable control
  marked with an amber ring.


## v0.1.0

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
