# M3 plan — faster compensation (2026-11-24)

> Audience: AI/owner process material (ledger). Facts + owner rulings;
> rationale quoted from the session that produced them. Successor of
> `m2-plan.md`. Contract text lands in `MakeGain_FR-v1.md` §10.11 (A5)
> before any code, per the frozen-FR rule.

## Owner rulings (2026-11-24, this session)

- **Two modes, one control.** The independent Metric and Response
  switches merge into one Mode switch: **Fast** (broadband level, short
  span) and **Smooth** (K-weighted loudness, 3 s). Three modes are worse
  than two, so the 400 ms LUFS combination retires.
- **The two HP reasons are kept separate** (owner: "we are here
  conflating a more technical reason"). The product reason is multiband
  compression in the low end pumping the delta (FR-26, unchanged,
  user-controlled, both modes). The technical reason is that a short
  window cannot resolve low frequencies at all.
- **Technical floor, direction corrected by the owner** ("I made math
  wrong it is max not min"): the Fast detector's effective corner is
  `max(HP setting, 80 Hz)`, and 80 Hz applies even with the HP off.
- **FR-26 default stays OFF**, corner default 150 Hz, range 20–400 Hz
  unchanged. The floor removes the pressure to change it: Fast is safe
  with the HP off.
- **Docs deliverable includes human docs**, not only ledgers: the
  behaviour must be readable by an end user in `doc/human/usage.md`.

## Contract delta (A5 — operative text, proposed)

- **FR-23 superseded.** The response parameter becomes the Mode
  parameter: `Fast` = broadband level, ~15 ms span, evaluated on a 5 ms
  grid; `Smooth` = BS.1770 short-term loudness, 3 s window, 100 ms grid.
  Everything else (clamp, hold, ramp, freeze, HP) is shared.
- **FR-6 / FR-8 amended.** The metric is no longer independently
  switchable; it follows the mode. No cross-metric math, no re-capture:
  both detectors run continuously, so a mode switch re-targets at the
  next evaluation with no gap.
- **FR-26 amended.** The metering HP stays optional, default OFF,
  20–400 Hz. Added: in Fast the effective corner is floored at 80 Hz
  because a ~15 ms window cannot resolve below ~67 Hz; the floor is part
  of the detector definition, not a user setting.
- **FR-12 / §10.6 readouts amended.** The delta readout carries the
  mode's unit and name: `chain delta … dB` (Fast, level) vs
  `chain delta … LU` (Smooth, LUFS).
- **FR-11 unchanged.** Still no added latency and no lookahead.

## Engine design

- **`FastLevel`** (new, JUCE-free, `src/dsp/`): one exponential
  mean-square follower per tap, τ = 15 ms, per-sample, broadband. Not a
  short loudness window: a rectangular 15 ms block ripples at the
  detector band's low end, a one-pole does not.
- **HP chain in Fast**: one 4th-order Butterworth HP per tap at
  `corner = HP_on ? max(HP_hz, 80) : 80`. One filter, not a cascade —
  when the user's corner is already above 80 it *is* the corner.
  Smooth keeps today's stage exactly as it is.
- **Evaluation grid**: the slot length becomes mode-derived (Fast 5 ms,
  Smooth 100 ms). Chunk invariance (R-D8) must hold for both grids.
- **Silence streaks become time-based** (5 s), not slot-count-based:
  `kNoSignalSlots = 50` only meant 5 s at a 100 ms slot.
- **Estimator**: dB difference of the short RMS (keeps one meaning for
  the delta readout across modes). Least-squares gain `Σwd/Σdd` is
  deferred, not rejected.
- **Both detectors always run** in both modes: the switch is free, the
  Smooth window stays warm, and FR-8's "no re-capture" honesty holds.
- **Target policy, ramp, clamp, hold, freeze**: unchanged.

## Persistence

- New `mode` choice parameter (0 = Fast, 1 = Smooth, default Fast).
- Legacy migration on load: `metric = LUFS → Smooth`,
  `metric = dB RMS → Fast`, `response` ignored. Preserves what an
  existing session was hearing. Verified by owner smoke on an old
  session (no unit test: the ValueTree layer is not in the JUCE-free
  test target — stated, not skipped silently).

## Commits

Two docs commits first (contract, then this ledger), then three PRs.
Every commit rides a branch (AGENTS.md rule 6 — no direct `main` work);
the two docs commits open the M3a branch and may land without the
per-commit eyeball (rule 4's docs exemption). Every source-touching
commit keeps the eyeball gate.

| # | PR / branch | Content | Kind | Gate |
|---|---|---|---|---|
| C1 | M3a `pr-m3a-fast-level` | `MakeGain_FR-v1.md` §10.11 A5 | docs | owner reads text |
| C2 | M3a | this plan; `decisions.md` rows; `backlog.md` M3 status | docs | owner reads text |
| C3 | M3a `pr-m3a-fast-level` | **RED**: `tests/dsp/fast_level_test.cpp` + honest stub `src/dsp/fast_level.h` + CMake test wiring; failing output quoted | source | eyeball + failing log |
| C4 | M3a | **GREEN**: `src/dsp/fast_level.cpp` | source | eyeball + green log |
| C5 | M3b `pr-m3b-mode-engine` | **RED**: `tests/dsp/gain_computer_test.cpp` additions (R-G1..R-G6) + stub changes; failing output quoted | source | eyeball + failing log |
| C6 | M3b | **GREEN**: `src/dsp/gain_computer.{h,cpp}` (mode, grid, floor, time-based silence) | source | eyeball + green log |
| C7 | M3b | **GREEN**: `src/plugin/MakeGainProcessor.{h,cpp}` (`mode` param, wiring, legacy migration) | source | eyeball + green log |
| C8 | M3c `pr-m3c-ui-docs` | UI iteration: one Mode pill, span caption in the freed flank, freeze lock, delta captions | source | owner eyeball on host **before** commit |
| C9 | M3c | Human docs: `usage.md` panel table + detector band, `code-tour.md` file map, `CHANGELOG.md` Unreleased | docs | owner reads text |

Sizing: 9 commits; source files per PR — M3a 3 (+CMake), M3b 5, M3c 2.
All inside the AGENTS.md reference metrics.

## Proposed test IDs (RED commits)

- **R-G1** Fast converges after a chain edit within a stated bound
  (target reached ≪ Smooth's).
- **R-G2** Floor rule: Fast corner is 80 with HP off, 80 at HP 40,
  the user's value at HP 150; Smooth has no floor.
- **R-G3** Mode switch mid-run: no gap, no reset, target re-aims at the
  next evaluation, ramp carries the change (no slam).
- **R-G4** Silence hold is time-based: 5 s of digital silence holds the
  gain on either grid; 4.9 s does not.
- **R-G5** Chunk invariance holds on the 5 ms grid (R-D8 analogue).
- **R-G6** Identical taps give an exact 0 dB delta on both paths.

## Open / deferred

- Least-squares gain estimator on the Fast path.
- Whether the freed right flank keeps the span caption permanently.
- Per-mode stored HP default (rejected for now: one value, default OFF).
