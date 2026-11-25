# M3 plan — faster compensation (2026-11-24)

> Audience: AI/owner process material (ledger). Facts + owner rulings;
> rationale quoted from the session that produced them. Successor of
> `m2-plan.md`. Contract text lands in `MakeGain_FR.md` §10.3 (modes)
> and §10.2 (measurement) before any code, per the frozen-FR rule.

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
- **Human docs are snapshots, the FR included** (owner correction,
  same session): the FR carries no amendment sections. It was rewritten
  as a current-state contract; the old text stays in git, the rulings
  here and in `decisions.md`. Recorded as AGENTS.md rule 12.

## Contract delta (A5 — what the rewrite changed)

- **FR-23 replaced by FR-28.** The response parameter becomes the Mode
  parameter: `Fast` = broadband level, ~15 ms span, evaluated on a 5 ms
  grid; `Smooth` = BS.1770 short-term loudness, 3 s window, 100 ms grid.
  Everything else (clamp, hold, ramp, freeze, HP) is shared.
- **FR-6 / FR-8 folded into FR-28.** The metric is no longer
  independently switchable; it follows the mode. No cross-metric math,
  no re-capture: both detectors run continuously, so a mode switch
  re-targets at the next evaluation with no gap.
- **FR-26 keeps its control, gains a neighbour.** The metering HP stays
  optional, default OFF, 20–400 Hz. In Fast the effective corner is
  floored at 80 Hz because a ~15 ms window cannot resolve below ~67 Hz;
  the floor is part of the detector definition, not a user setting
  (FR-29).
- **FR-12 / §10.6 readouts.** The delta readout carries the mode's unit
  and name: `chain delta … dB` (Fast, level) vs `chain delta … LU`
  (Smooth, LUFS).
- **FR-11 unchanged.** Still no added latency and no lookahead.

## Old anchors (code comments and ledger rows)

The FR is a snapshot, so the v1 body and its amendment stack are gone;
their text lives in git and in `decisions.md`. References written
against them resolve here — this table is the map AGENTS.md rule 12
requires:

| Old reference | What it meant | Now |
|---|---|---|
| §1–§9 | v1 body: product story, Idle/Read/Adjust FSM, UX table | §1, §10.2 |
| §10.0 | A2 amendment fate table | `decisions.md` |
| §10.7 | SRC/DST two-instance pair (rejected) | `jsfx/*.jsfx` header comments |
| §10.9 | A3: response, freeze, ramp, meter HP | §10.2 (FR-24/25/26) |
| §10.10 | A4: panel zoom | §10.6 (FR-27) |
| §10.11 | A5: two compensation modes | §10.3 (FR-28/29/30) |
| FR-1 | Idle: pass audio, apply no gain | §10.2 (engagement) |
| FR-2 | Read: fixed 3 s training window | — (no training) |
| FR-3 | Adjust: compare against a stored reference | §10.2 |
| FR-4 | Re-read: replace the reference | — (no reference) |
| FR-5 | Active toggle, reference preserved | §10.2 (engagement) |
| FR-6, FR-6a | Metric options; both captured per training run | FR-28 (mode) |
| FR-7 | Reference object persisted with the session | FR-22 (no captured reference) |
| FR-8 | Metric switching is free, no re-capture | FR-28 (both detectors always run) |
| FR-9 | Silence handling | §10.4 |
| FR-13 | Mode controls (Read / Adjust / Active) | FR-14 |
| FR-23 | Response: Fast 400 ms / Smooth 3 s | FR-28 (Fast 15 ms / Smooth 3 s) |
| UX-Q1, Q2, Q6 | Training window; both metrics captured per run | — (no training) |
| UX-Q3 | Sustained silence holds the last gain | §10.4 |
| UX-Q4 | Only Active is automatable | FR-15 |
| UX-Q5 | dB RMS is one mono-summed value | §10.2 |

jsfx headers and `src/dsp/loudness_meter.{h,cpp}`,
`src/plugin/MakeGainProcessor.{h,cpp}` still cite v1 ids (FR-2/FR-8/
FR-23, UX-Q4/Q5, A2/A3 tags); those resolve through this table, and
M3b/M3c rewrite the ones they touch anyway.

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
| C1 | M3a `pr-m3a-fast-level` | `MakeGain_FR.md` §10.3 (modes), whole-file snapshot rewrite | docs | owner reads text |
| C2 | M3a | this plan; `decisions.md` rows; `backlog.md` M3 status | docs | owner reads text |
| C3 | M3a `pr-m3a-fast-level` | **RED**: `tests/dsp/fast_level_test.cpp` + honest stub `src/dsp/fast_level.h` + CMake test wiring; failing output quoted | source | eyeball + failing log |
| C4 | M3a | **GREEN**: `src/dsp/fast_level.cpp` | source | eyeball + green log |
| C5 | M3b `pr-m3b-mode-engine` | **RED**: `tests/dsp/gain_computer_test.cpp` additions (R-G1..R-G8) + honest stub, legacy cases re-pinned to Smooth; failing output quoted | source | eyeball + failing log |
| C6 | M3b | **GREEN**: `src/dsp/gain_computer.{h,cpp}` (mode, grid, floor, time-based silence) | source | eyeball + green log |
| C7 | M3b | **GREEN**: `src/plugin/MakeGainProcessor.{h,cpp}` — reuse `response` as the mode selector (default Fast), leave `metric` a dead switch, delete the interim engine shim + retire R-G8; `mode` param/migration deferred to C8 (owner, 2026-11-25) | source | eyeball + green log |
| C8 | M3c `pr-m3c-ui-docs` | UI iteration: one Mode pill, span caption in the freed flank, freeze lock, delta captions | source | owner eyeball on host **before** commit |
| C9 | M3c | Human docs: `usage.md` panel table + detector band, `code-tour.md` file map, `CHANGELOG.md` Unreleased | docs | owner reads text |

Sizing: 9 commits; source files per PR — M3a 3 (+CMake), M3b 5, M3c 2.
All inside the AGENTS.md reference metrics.

**Status**: C5–C7 are on `pr-m3b-mode-engine`, plus a build fix
(`d9d5ae5`: `fast_level.cpp` was missing from the plugin target) and the
ledger updates. The owner host-accepted M3b at `d9d5ae5` — verdict is
perceptual (see `decisions.md`). C8/C9 open.

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
- **R-G7** Mode change refused while frozen (FR-30): the mode holds, the
  gain holds, and the switch works again after release.
- **R-G8** Legacy shim: default mode is Fast, `set_metric(LUFS)` →
  Smooth, `set_metric(dB RMS)` → Fast, `set_meter_window` is a no-op.

R-D4 (metric pairing) retires into R-G3; R-F3/R-F4 (Response window)
into R-G1/R-G3. Every legacy A2/A3 case explicitly pins **Smooth**, so
it keeps pinning today's stage across the default flip to Fast.

### M3b rulings

- Interim shim (owner, 2026-11-25; **superseded by the C7 ruling
  below**): the panel's Metric switch drove the Mode
  (`LUFS → Smooth`, `dB RMS → Fast`) and `set_metric` /
  `set_meter_window` lived in the engine as shims through C6.
- C7 wiring (owner, 2026-11-25, supersedes the Metric→Mode shim row):
  minimal GUI this phase. The Response switch drives the mode
  (`Short → Smooth`, `Fast → Fast`, default flipped to Fast); Metric
  stays bound but ignored. The engine shims and R-G8 retire here; the
  `mode` parameter and legacy migration land with the C8 panel.
- RED-gap note: at the stub's fixed 100 ms grid, R-G4/R-G6/R-G8 pass by
  construction — they guard the C6 grid change, not the stub's absence
  of it. Stated in the C5 message rather than hidden.


## Open / deferred

- Least-squares gain estimator on the Fast path.
- Whether the freed right flank keeps the span caption permanently.
- Per-mode stored HP default (rejected for now: one value, default OFF).
