# MakeGain — Functional Requirements (FR)

> Version: v1.1 — 2026-10-16 (UX-Q1..Q6 resolved; see §7)
> Product: chain-wide automatic make-gain / gain compensation plugin.
> Working name: **MakeGain** · repo: `auto-make-gain`
>
> This document captures *what the plugin must do*, not *how*. Tech specs live
> in `../ai/` and will be written against these FRs once frozen.
>
> **2026-10-22 — Amendment A2 appended (§10).** It supersedes named parts of
> §1–§8 (§10.0 lists every fate). The v1 body above stays untouched for
> history; for anything it contradicts, §10 is operative.

---

## 1. Product story (context for the FRs)

The user adds **MakeGain as the first plugin in a chain**, before anything
else. It measures the clean, unprocessed signal for a few seconds (**Read
phase**) and memorizes its loudness as the *reference*. The user then switches
MakeGain to **Adjust phase** and starts inserting other plugins (EQ,
saturator, compressor, ...) before it. MakeGain now sits after the chain,
compares the processed signal's loudness against the memorized reference, and
applies enough gain to keep the perceived loudness constant.

No sidechain input. No external routing. One instance, one insert point.

## 2. Modes / state machine

```
      ┌────────┐  user presses Read   ┌──────────┐   analysis sufficient →
      │  IDLE  │──────────────────────►│   READ   │  user presses Adjust     ┌─────────┐
      └────────┘                       └──────────┘──────────────────────────►│ ADJUST  │
         ▲    ▲                                │                              └─────────┘
         │    └──────────(from either state)──┘                                   │
         │            Active off                                                  │
         └────────────────────────────────────────────────────────────────────────┘
                    Re-read: ADJUST ──► READ again at any time
```

- **FR-1 IDLE** — plugin passes audio through untouched (no gain applied).
  Metering of in/out level may run.
- **FR-2 READ (training)** — user presses the training button (Melda-style):
  the plugin keeps passing audio untouched while it measures the input
  loudness for a fixed training window. Duration is **not a UI setting**: it
  lives in code as a single variable (default **3.0 s**, adjustable in code
  without touching the FR contract). When the window elapses, training
  completes on its own (no second press needed) and captures the reference.
  The plugin remains un-engaged (no gain applied) until the user presses
  Adjust (FR-3).
- **FR-3 ADJUST** — plugin continuously measures the loudness of its (now
  processed) input with the **selected metric** and applies compensating gain
  `g = reference(selected metric) − measured(selected metric)`, ramped
  smoothly per-sample (no zipper noise, automation-safe).
- **FR-4 Re-read** — from any state, pressing the training button starts a
  new training run (replaces the previous reference at its next completion).
- **FR-5 Active toggle** — an engage/bypass switch that returns to IDLE and
  back, preserving the last reference (no re-train on re-engage).
- **FR-5a** If audio is absent (silence) during the training window, the
  captured reference may be poor — see FR-9 and UX-Q3-style behavior: the
  plugin still captures, but flags "low-confidence reference" in the UI so
  the user can re-read. (Behavior of silence inside training is simple:
  it counts as measured signal; flag, don't block.)

## 3. Measurement (behavioral requirements)

- **FR-6 Metric options** — compensation metric is a runtime switch:
  - **LUFS** (perceived loudness, K-weighted short-term scale) — default.
  - **dB RMS** (traditional average-power decibels) — alternative.
- **FR-6a Both metrics captured during every training run.** One training
  produces *two* reference values (LUFS and dB RMS) computed over the same
  window. The metric switch simply selects which pair the Adjust comparison
  uses — switching **never invalidates** the reference (see FR-8).
- **FR-7 Reference object** — stored per instance: {LUFS reference, dB RMS
  reference, training window length used}. Persisted with the host session;
  survives project reload, plugin parameter changes, and host restarts.
- **FR-8 Metric switching is free** — selecting LUFS vs dB RMS at any time
  (including mid-Adjust) changes only which stored reference and which live
  measurement the gain computer compares. No re-read required, no
  cross-metric math ever occurs. Display of the "other" metric remains
  active for metering; no stale state exists in v1.
- **FR-9 Silence handling**:
  - during Read: silence is treated like any signal (FR-5a flags a
    low-confidence reference); training does not wait or pause.
  - during Adjust: sustained silence → hold last applied gain (UX-Q3).
- **FR-10 Compensation clamp** — applied gain is limited to ± MaxComp dB
  (user parameter; default **24 dB**). When clamped, the UI flags it
  ("clamped") so the user doesn't misread it as success.
- **FR-11 No added latency / PDC neutrality** — the plugin must not introduce
  sample delay beyond the host's normal routing (it is a pure gain
  application; nothing sample-based).

## 4. UI (functional, not visual spec)

- **FR-12 The UI always shows**: input loudness, output loudness, current
  applied gain, stored reference, current mode, metric.
- **FR-13 Mode controls**: Read button, Adjust button (labelled by state),
  Active toggle.
- **FR-14 Settings**: metric (LUFS | dB RMS), MaxComp, gain ramp speed.
- **FR-15 All parameters important for operation are automation-writable in
  the host** (at minimum: Active; others may be UI-only — see UX-Q4).

## 5. Platform / host / format requirements

- **FR-16 Formats**: VST3 + CLAP are the shipping pair. AU is built as an
  exercise tier (GarageBand). The read→adjust design requires **no
  sidechain**, therefore GarageBand support carries no degraded mode.
- **FR-17 Platforms**: macOS arm64 (primary) and Linux aarch64 / Asahi
  (primary). Linux x86_64 (best-effort, VM-limited testing). Consumers' hosts:
  Reaper (all platforms), Ardour (Linux/macOS), GarageBand (macOS, relaxed).
- **FR-18 Distribution (v0)**: source code in this public GitHub repo;
  "install" = clone + build locally. Proper installers/packages are a later
  milestone goal, not a v0 requirement.
- **FR-19 Open source**: repo public from day one (confirmed). License
  decision happens at tech-spec time (framework-dependent), not in this doc.

## 6. Quality requirements

- **FR-20 Real-time safety** — audio path must run lock-free & allocation-free
  (formal spec TBD in tech docs, behavior is: no clickouts/glitches at any
  UI/parameter rate).
- **FR-21 Determinism** — offline render and realtime playback of the same
  session produce the same audio.
- **FR-22 Persistence quality** — loading a session in a different host but
  same platform yields identical reference/settings (no host-specific state).

## 7. UX decisions (settled — v1)

| # | Decision |
|---|---|
| UX-Q1 | Training is a press-to-start capture running a fixed code-level window (default 3 s, var in code, not a UI control). Capture completes by itself; engaging Adjust is a separate explicit press. |
| UX-Q2 | Same as UX-Q1: window is 3 s by default, code-adjustable. |
| UX-Q3 | On sustained silence during Adjust: **hold last gain** (do nothing), show "no signal". |
| UX-Q4 | Only the Active toggle is automatable. Metric switch, MaxComp, ramp speed are UI-only (non-automatable parameters). |
| UX-Q5 | dB RMS is one mono-summed value in v1. Stereo/dual-mono metering is future milestone work. |
| UX-Q6 | **Both references (LUFS + dB RMS) are captured in every training run.** Switching metrics never invalidates anything; no stale state exists in v1. |

## 8. Non-goals (v1)

- No sidechain/dual-instance reference mode.
- No per-frequency (spectral) compensation.
- No loudness targeting for delivery/mastering (-14 LUFS style target
  matching).
- No installers/packaging in v0 (FR-18).
- No presets system beyond a saved default state (may be revisited).

## 9. Deferred ideas (backlog, not v1)

- **Noise/silence skipping in the computation** — skip quiet content instead
  of counting it, so training and Adjust ignore noise floors entirely.
  Noted for a future version. Implies committing to a fixed, defined
  noise/silence gate threshold (absolute gate). Deferred in v1 only; the
  FR text (FR-5a, FR-9) should be revisited when this is designed so the
  gate definition exists in exactly one place.

---

## 10. Amendment A2 — internal-sidechain AGC (2026-10-22, owner ruling)

> Written after smoke-testing the leveler reading of FR-3 in Reaper (the
> "duck test"): a single measurement tap cannot distinguish an edit from
> program dynamics, and the ear-safety incident exposed a
> trained-reference model willing to compensate from a garbage reference.
> The v1 body above stays for history; this section is the operative
> contract for all current and future work.

### 10.0 What A2 supersedes, and what survives

| Item | Fate under A2 |
|---|---|
| §1 story; §2 FSM (IDLE/READ/ADJUST); FR-2, FR-4, FR-5a, FR-6a | **Retired** — no training phase, no Read button, no stored reference. |
| FR-3 | **Replaced** (10.2). |
| FR-5 toggle semantics; FR-9 hold; FR-10 clamp; FR-11 latency; FR-20/FR-21 | **Kept** (hold/clamp re-derived on the delta engine, 10.4). |
| FR-6 metric options; FR-8 metric-switching freedom | **Kept** — applied to the delta pairing (switching needs no re-capture). |
| FR-7 reference persistence | **Retired** — persisted state = parameters only (FR-22 narrows accordingly). |
| FR-12 UI readouts | **Re-derived** (10.6). |
| FR-13 Read button | **Retired** — controls: Active, Metric, MaxComp, Ramp (FR-14 unchanged). |
| FR-16 AU/GarageBand | **Amended** (10.5). |
| §7 UX-Q1/Q2/Q6 | **Retired with training**; UX-Q3/Q4/Q5 survive in re-derived form. |
| §8 "No sidechain/dual-instance reference mode" | **Amended** (10.7). |

### 10.1 Architecture: one instance, two stereo input pairs

MakeGain is **one plugin** with **two stereo inputs** — main = chain-
processed ("wet") and aux = pre-chain tap ("dry") — and one stereo
output. The dry tap travels over the host's own audio plumbing; the
plugin performs no routing magic:

- **Bus rule**: the aux bus mirrors the main bus's channel count. A
  genuinely mono track yields a mono main bus and a **mono aux (one pin
  — pin 2)**; stereo mains get the stereo aux pair (pins 3/4). The
  engine consumes mono sums (UX-Q5), so it is channel-count agnostic;
  only pin numbering changes.
- **Reaper (recommended path)**: the shipped, optional **MakeGain TAP**
  JSFX is inserted first in the FX chain and copies channels 1/2 onto
  3/4, bit-exact (4 output pins). Reaper's serial chain flow carries
  3/4 down to the plugin's aux input pins; intermediate stereo FX leave
  3/4 untouched; Reaper raises the track channel count automatically on
  insert.
- **Plain routing fallback (all hosts)**: any sidechain route the host
  offers (Reaper: track send into channels 3/4 from a clean dry track;
  Ardour: the plugin's native sidechain hookup). Documented caveat: a
  routed dry copy is static and diverges from later edits of the main
  items ("stale tap" — a risk the in-chain TAP does not have).
- The **TAP is optional** — a convenience layer, not part of the
  protocol (owner ruling, 2026-10-22).

### 10.2 Core semantics: null the chain's delta

The applied-gain target is evaluated every 100 ms slot, per selected
metric:

    target = −( loudness(wet) − loudness(dry) )

then ramped per-sample (FR-5's one-pole, tc = ramp speed). Invariants
the tests pin:

- **Program dynamics pass through**: a level change present in BOTH
  taps (normal musical motion) leaves the delta unchanged — the applied
  gain does not chase the music. In steady state, output loudness =
  dry loudness.
- **Edits are compensated**: any change in the chain between the two
  taps (enable/configure/bypass an FX) moves wet only — the gain nulls
  it within the ramp.
- Honest implication: on *dynamic* chains (compressors, saturators —
  anything whose processing delta varies with the program) the AGC
  partially nulls that gain modulation. That is gain compensation by
  definition; users who reject it disengage Active.

### 10.3 Modes

- **Idle (Active off)** — unity, ramped to 0 dB (FR-5 semantics kept).
- **Engaged (Active on)** — delta-nulling per 10.2; engagement always
  starts the ramp from the current applied gain — never a slam.
- No READ phase exists.

### 10.4 Silence, tap loss, ear safety

- **Delta validity**: both taps' newest wall slot must be above the
  digital-silence floor (existing wall-slot gate machinery). If EITHER
  tap is silent, the delta is meaningless → **hold last applied gain**
  (FR-9 re-derived); resume when valid audio returns to both taps.
- `no_signal`: wet-side silence sustained ≥ 5 s flags the UI (TS §9 ③
  gate unchanged). Aux-side silence sustained ≥ 5 s flags
  **"dry tap missing"** — the plugin cannot, and does not claim to,
  distinguish an unconnected tap from genuinely silent source audio;
  in both cases the gain holds.
- **Ear safety profile**: with no trained-reference regime, the
  leveler-era pathology (compensating from a garbage reference into a
  large boost slam) is structurally impossible. All target moves ramp;
  MaxComp's default is lowered **24 → 16 dB** (range 0–48 unchanged,
  parameter arrives stage 2).

### 10.5 Host/format matrix (amended; GarageBand dropped)

- Hosts of record: **Reaper** (mac + Linux) and **Ardour** (mac +
  Linux). **GarageBand is removed from the matrix** (owner,
  2026-10-22): the GarageBand AU exercise gate retires.
- **AU stays a best-effort build** (same standing as the x86_64 lane):
  it is compiled and shipped, but carries no validated-behavior claims.
- Latency note (accepted v1 limitation): chain PDC between the dry tap
  and the plugin shifts wet relative to dry. Slow loudness windows
  absorb small skews; no per-sample alignment is attempted (FR-11
  spirit: no added latency of our own).

### 10.6 UI (re-derived, FR-12)

Always shows: dry loudness, wet loudness, chain delta (wet − dry,
selected metric), applied gain, Active state, metric; flags: `clamped`,
`no_signal` (wet), `dry tap missing` (aux). No reference readout exists.

### 10.7 Rejected architecture, kept on record

A two-plugin SRC/DST pair ("clamp") communicating over undocumented
inter-instance IPC was evaluated as a routing-automation idea and
**rejected**: the dry data must still travel, and out-of-spec
transports break under host sandboxing, instance pairing, and
multi-pair projects. The host's own audio plumbing is the robust
transport. (A Reaper-only convenience variant over standard FX pin
patching remains a possible future addendum, not v1.)

### 10.8 TAP spec (Reaper convenience layer)

Two text files (e.g. `plugins/MakeGainTap.jsfx`,
`plugins/MakeGainTapMono.jsfx`):

- **Tap (stereo chains)**: declares 4 output pins; 3/4 = 1/2, bit-exact.
- **TapMono (mono chains)**: declares 2 output pins; 2 = 1, bit-exact
  (a mono main's aux is pin 2).

No math in either. Imported once, inserted first in a chain. Not
required for AGC operation — plain routing works everywhere.

**Routing rule for stereo mains** (both paths): the dry tap must occupy
BOTH aux channels — mono content is duplicated to 3/4 (the TAP does
this inherently). A mono-only feed into one aux channel reads 6 dB too
quiet in the aux mono-sum and overcompensates by that skew. The plugin
deliberately does NOT auto-detect this (hard-panned content in the aux
is legitimate audio); it is a documented misroute, not a code branch.

### 10.9 Amendment A3 — responsiveness, freeze, metering HP (2026-10-25, owner rulings)

The A2 AGC is tuned for usability with three new controls. All are UI-
only non-automatable parameters; all persist with the session (FR-22).

**FR-23 Meter response.** One parameter, two standardized responses:

- **Short (S)** — 3 s measurement window (the A2 default; the ITU
  BS.1770 short-term geometry).
- **Fast (M-convention)** — 400 ms measurement window, refreshed every
  100 ms; the ITU momentary geometry on the A2 slot grid.

Both share the identical slot machinery, metric, ramp and hold rules;
only the measurement window (and thereby target liveliness/convergence
time) changes. Fast converges ~4–7× quicker after a chain edit and is
stabbed more easily by program low-end movement; stability is the
user's via response + meter HP (below) + Freeze.

**FR-24 Gain freeze.** A toggle that converts the compensator into a
**static volume adjustment**: engaging Freeze locks the applied gain
at its current value (metering idles — no evaluation, no ramping, no
further gain movement). Releasing restores evaluation with the ramp
starting from the held value toward the live target — continuous, no
jump. Frozen state persists across save/reload.

**FR-25 Ramp default lowered.** Default ramp tc 300 ms → **50 ms**
(decisions register, 2026-10-23/25): the fast feel owners actually use.
Range unchanged (floor 10 ms — PR-B3).

**FR-26 Metering high-pass (optional, default OFF).** A high-pass
**in the metering path only** — applied identically to wet and dry
meter feeds, the audio path is untouched (no coloration ever reaches
the signal). Parameters: enable toggle + corner frequency 20–400 Hz
(default 150), slope fixed at 24 dB/oct (4th-order Butterworth).
Purpose: dynamic EQs / multiband chains pump the low end; the energy
dominance of bass then drives the AGC's delta. HP makes that band
invisible to compensation, exactly as low-cut crosstalk isolation in
crossover/analysis practice. Honest semantics: with HP engaged, the
compensation targets the *filtered* loudness interval (documented
readout implication); symmetric wet/dry filtering keeps musical level
motion out of the delta (10.2 invariant unchanged).
