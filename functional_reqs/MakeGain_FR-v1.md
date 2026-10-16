# MakeGain — Functional Requirements (FR)

> Version: v1.1 — 2026-10-16 (UX-Q1..Q6 resolved; see §7)
> Product: chain-wide automatic make-gain / gain compensation plugin.
> Working name: **MakeGain** · repo: `auto-make-gain`
>
> This document captures *what the plugin must do*, not *how*. Tech specs live
> in `../tech_specs/` and will be written against these FRs once frozen.

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
