# MakeGain — Functional Requirements (FR)

> Product: chain-wide automatic make-gain / gain compensation plugin.
> Working name: **MakeGain** · repo: `auto-make-gain`
>
> This document is the contract as it stands: what the plugin must do
> now. The reasoning, dated rulings and earlier designs live in `doc/ai/`
> (`decisions.md`, `m1-plan.md`, `m2-plan.md`, `m3-plan.md`) and in git.
> Requirement numbers (FR-n) and section numbers (§10.x) are stable
> identifiers used by code comments and by the ledgers; a number that no
> current requirement uses is never reused for something else.

---

## 1. Product story

MakeGain keeps the loudness of a processing chain steady. It is inserted
**after** the plugins whose level it should compensate, and it receives
two signals: the **wet** input (chain-processed) and a **dry** tap taken
before the chain. It applies one gain that nulls the difference between
them, so inserting, editing or bypassing any plugin in the chain changes
the level of the chain but not of the output.

Loudness here is not a musical judgement: it is one measured delta
between the two taps, re-evaluated continuously and applied through a
ramp. Program dynamics reach both taps and therefore cancel; only
processing that moves the wet tap alone is compensated.

## 2. Non-goals

- No per-frequency (spectral) compensation: one broadband gain.
- No loudness targeting for delivery (-14 LUFS style mastering targets).
- No presets system beyond a saved default state.
- No installers or packages in v0 (FR-18).
- Noise/silence gating inside the measurement — skip-quiet-content
  measurement is deferred, tracked in `doc/ai/backlog.md`.

## 3. Quality requirements

- **FR-20 Real-time safety** — the audio path is lock-free and
  allocation-free; no clickouts or glitches at any UI or parameter rate.
- **FR-21 Determinism** — offline render and realtime playback of the
  same session produce the same audio.
- **FR-22 Persistence** — settings persist with the host session and
  survive plugin-instance restart; loading the session on another host
  on the same platform yields identical state. Persisted state is the
  parameter set only: there is no captured reference to store.

## 4. Distribution, formats, platforms

- **FR-16 Formats** — VST3 and CLAP are the shipping pair. AU is built
  and shipped as a best-effort build with no validated-behaviour claims
  (same standing as the x86_64 lane).
- **FR-17 Platforms and hosts** — macOS arm64 and Linux aarch64 (Asahi)
  are primary; Linux x86_64 is best-effort. Hosts of record: **Reaper**
  (macOS, Linux) and **Ardour** (macOS, Linux).
- **FR-18 Distribution (v0)** — source in this public repository;
  "install" means clone and build locally.
- **FR-19 Open source** — the repository is public from day one; the
  license follows the framework route (AGPLv3, see `LICENSE`).

---

## 10. Engine contract

### 10.1 Architecture: one instance, two stereo input pairs

MakeGain is **one plugin** with **two stereo inputs** — main =
chain-processed ("wet") and aux = pre-chain tap ("dry") — and one stereo
output. The dry tap travels over the host's own audio plumbing; the
plugin performs no routing magic.

- **Bus rule**: the aux bus mirrors the main bus's channel count. A
  genuinely mono track yields a mono main bus and a **mono aux (one pin
  — pin 2)**; stereo mains get the stereo aux pair (pins 3/4). The
  engine consumes mono sums (see §10.2), so it is channel-count
  agnostic; only pin numbering changes.
- **Reaper (recommended path)**: the optional **MakeGain TAP** JSFX is
  inserted first in the FX chain and copies channels 1/2 onto 3/4,
  bit-exact (4 output pins). Reaper's serial chain flow carries 3/4 down
  to the plugin's aux input pins; intermediate stereo FX leave 3/4
  untouched; Reaper raises the track channel count automatically on
  insert.
- **Plain routing fallback (all hosts)**: any sidechain route the host
  offers (Reaper: track send into channels 3/4 from a clean dry track;
  Ardour: the plugin's native sidechain hookup). Caveat: a routed dry
  copy is static and diverges from later edits of the main items (a
  *stale tap* — a risk the in-chain TAP does not have).
- The TAP is optional: a convenience layer, not part of the protocol.
- No inter-instance IPC exists or is assumed; the host's audio plumbing
  is the only transport.

### 10.2 Signal flow and gain policy

The applied-gain target is evaluated every evaluation slot by the active
mode's detectors (see §10.3), per tap:

    target = −( reading(wet) − reading(dry) )

where `reading` is the active mode's detector (level in Fast, loudness in
Smooth).

then ramped per sample and clamped. Invariants the tests pin:

- **Program dynamics pass through**: a level change present in BOTH taps
  (normal musical motion) leaves the delta unchanged — the applied gain
  does not chase the music. In steady state, output loudness = dry
  loudness.
- **Edits are compensated**: any change in the chain between the two
  taps (enable/configure/bypass an FX) moves wet only — the gain nulls
  it within the ramp.
- Honest implication: on *dynamic* chains (compressors, saturators —
  anything whose processing delta varies with the program) the
  compensator partially nulls that gain modulation. That is gain
  compensation by definition; users who reject it disengage Active.

Both taps are reduced to one mono sum before measurement.

**Engagement.** With Active off the plugin passes audio at unity,
ramping to 0 dB from wherever the applied gain currently is.
With Active on it nulls the delta. Engagement never slams: it always
starts from the current applied gain.

- **FR-10 Compensation clamp** — applied gain is limited to ± MaxComp dB
  (user parameter, default **16 dB**, range 0–48). While the target sits
  beyond the bound the UI flags `clamped`, so it cannot be misread as
  success.
- **FR-25 Gain ramp** — one-pole per-sample ramp; default time constant
  **50 ms**, floor 10 ms. Automation-safe, no zipper noise.
- **FR-24 Gain freeze** — a toggle that converts the compensator into a
  static volume adjustment: engaging Freeze locks the applied gain at
  its current value (no evaluation, no ramping, no further gain
  movement). Releasing restores evaluation with the ramp starting from
  the held value — continuous, no jump. The frozen state persists with
  the session.
- **FR-26 Metering high-pass** — optional, default **OFF**, corner
  **150 Hz**, range 20–400 Hz, slope fixed at 24 dB/oct (4th-order
  Butterworth). It lives in the **metering path only**, applied
  identically to wet and dry, so it can never colour the audio. Its
  purpose is product-level: dynamic EQs and multiband chains pump the
  low end, and bass energy dominance would otherwise drive the delta.
  Honest semantics: with it engaged, compensation targets the
  *filtered* loudness interval, and the readouts mean the filtered
  value.

### 10.3 Modes (FR-28)

One two-position, non-automatable parameter chooses the detector pair.
The metric follows the mode; it is not independently switchable.

- **Fast** (default) — broadband level: exponential mean square,
  τ = 15 ms, per sample, evaluated on a 5 ms internal grid. A *level*
  reading, not a loudness reading; converges on a chain edit in tens of
  milliseconds.
- **Smooth** — BS.1770 short-term loudness: K-weighted, 3 s window,
  100 ms internal grid. The psychoacoustically representative reading,
  and correspondingly slower.

Both detectors run continuously in both modes, so switching needs no
re-capture and no fill wait. Switching is allowed at any time while not
frozen; the ramp carries the target change (no slam). Session load
honours the parameter names `metric` and `response`: `metric = LUFS`
selects Smooth, `metric = dB RMS` selects Fast, and `response` is
ignored.

- **FR-29 Fast detector band floor** — a ~15 ms span cannot resolve
  below ~67 Hz, so the Fast path applies a 4th-order high-pass at an
  effective corner of `max(Meter HP corner, 80 Hz)`, with 80 Hz applied
  even when the Meter HP is off. The floor is part of the detector
  definition, not a user setting, and it is separate from the FR-26
  product reason. Smooth has no floor.
- **FR-30 Mode and Freeze** — the Mode switch is locked while Freeze is
  engaged: a metering-definition change during a hold would move the
  readouts under the hold and leave a different detector state at
  release.

### 10.4 Silence, tap loss, ear safety

- **Delta validity**: both taps' newest slot must be above the
  digital-silence floor (the wall-slot gate). If EITHER tap is silent
  the delta is meaningless → **hold the last applied gain**; evaluation
  resumes when valid audio returns to both taps.
- `no_signal`: wet-side digital silence sustained ≥ 5 s flags the UI.
  Aux-side silence sustained ≥ 5 s flags **`dry tap missing`** — the
  plugin cannot, and does not claim to, distinguish an unconnected tap
  from genuinely silent source audio; in both cases the gain holds.
- **Ear safety**: there is no captured reference anywhere in the
  design, so a stale or garbage reference cannot produce a large boost
  slam. Every target move is ramped, and MaxComp bounds the reach
  (§10.2).
- **Latency (FR-11)**: the plugin adds no sample delay and does no
  lookahead; it is pure per-sample gain. Chain PDC between the dry tap
  and the plugin shifts wet relative to dry; the measurement windows
  absorb small skews and no per-sample alignment is attempted.

### 10.5 Host and format matrix

Hosts of record are **Reaper** (macOS, Linux) and **Ardour** (macOS,
Linux). Format and platform standing is FR-16/FR-17. A routed dry copy
can be stale (§10.1) — the in-chain TAP cannot.

### 10.6 UI

- **FR-12 Always shown**: dry loudness, wet loudness, chain delta
  (wet − dry, in the active mode's unit and name: `dB` for Fast, `LU`
  for Smooth), applied gain, Active state, mode. Flags: `clamped`,
  `no_signal` (wet), `dry tap missing` (aux).
- The panel states the active detector's band and span.
- **FR-14 Controls**: Active, Mode, MaxComp, Ramp, Freeze, Meter HP
  with corner frequency, panel zoom.
- **FR-15 Automation**: Active is automatable. Everything else is a
  UI-only, non-automatable parameter (FR-22 class) — the user is the
  judge of these settings.
- **FR-27 Panel zoom** — manual zoom control in the status-bar corner:
  75 / 100 / 125 / 150 / 200 %, default 100 % (native rendering, no
  transform). The editor applies a uniform whole-window transform to a
  child panel and sizes the window around it; no auto-DPI guessing. The
  level persists with the session. The visual contract (column grid,
  hero protection, colour vocabulary) lives in
  `doc/ai/visual-grammar.md` and applies at every zoom step.

### 10.8 TAP spec (Reaper convenience layer)

Two text files (e.g. `jsfx/MakeGainTap.jsfx`,
`jsfx/MakeGainTapMono.jsfx`):

- **Tap (stereo chains)**: declares 4 output pins; 3/4 = 1/2, bit-exact.
- **TapMono (mono chains)**: declares 2 output pins; 2 = 1, bit-exact
  (a mono main's aux is pin 2).

No math in either. Imported once, inserted first in a chain. Not
required for operation — plain routing works everywhere.

**Routing rule for stereo mains** (both paths): the dry tap must occupy
BOTH aux channels — mono content is duplicated to 3/4 (the TAP does this
inherently). A mono-only feed into one aux channel reads 6 dB too quiet
in the aux mono-sum and overcompensates by that skew. The plugin
deliberately does NOT auto-detect this (hard-panned content in the aux
is legitimate audio); it is a documented misroute, not a code branch.

