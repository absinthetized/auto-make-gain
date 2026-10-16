# MakeGain — Functional Requirements (FR)

> Version: v1 — 2026-10-16
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
- **FR-2 READ** — plugin keeps passing audio untouched while measuring the
  input loudness. Duration: at least a few seconds of program material; the
  plugin indicates readiness as the measurement stabilizes (progress +
  "ready" state). Reference is captured when the user confirms (see UX-Q1).
- **FR-3 ADJUST** — plugin continuously measures the loudness of its (now
  processed) input with the **same metric and display settings used at
  Read time**, and applies compensating gain `g = reference − measured`,
  ramped smoothly per-sample (no zipper noise, automation-safe).
- **FR-4 Re-read** — from any state, pressing Read starts a new training run
  (replaces the previous reference).
- **FR-5 Active toggle** — an engage/bypass switch that returns to IDLE and
  back, preserving the last reference (no re-train on re-engage).

## 3. Measurement (behavioral requirements)

- **FR-6 Metric options** — user selects the loudness metric *before/while
  training*:
  - **LUFS** (perceived loudness, K-weighted short-term scale) — default and
    primary.
  - **dB RMS** (traditional average-power decibels) — alternative.
- **FR-7 Reference object** — stored per instance: {metric, captured value,
  settings used}. Persisted with the host session; survives project
  reload, plugin parameter changes, and host restarts.
- **FR-8 Metric/ref consistency** — changing the metric after a reference
  exists invalidates it: reference shows as **stale** and compensation is
  not allowed until a new Read completes (no cross-metric comparison).
- **FR-9 Silence handling**:
  - during Read: silence does not pollute or corrupt the reference
    (training waits for actual program material);
  - during Adjust: behavior on sustained silence is a product decision —
    see UX-Q3 (default proposal: hold last gain).
- **FR-10 Compensation clamp** — applied gain is limited to ± MaxComp dB
  (user parameter; proposed default **24 dB**). When clamped, the UI flags
  it ("clamped" indication) so the user doesn't misread it as success.
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

## 7. UX decisions to settle (discussed in chat, then frozen here)

| # | Question | Proposal |
|---|---|---|
| UX-Q1 | How does Read end — user-confirmed, or auto after stability? | Proposal: progress fills as measurement stabilizes; user presses "Adjust" to accept (explicit, predictable). Optional "auto-engage when ready" setting later. |
| UX-Q2 | What short is "few seconds"? (min window length before Read can be accepted) | Proposal: minimum ~3 s of signal; Read button disabled/indicates "too short" below that. |
| UX-Q3 | Adjust behavior on sustained silence: hold last gain, or decay to 0 dB? | Proposal: hold last gain (do nothing), with visual "no signal" state. |
| UX-Q4 | Which parameters are automatable vs UI-only? | Proposal: Active automatable; metric/clamp/ramp not (avoid weird sessions). |
| UX-Q5 | dB RMS reference: integrate both channels to one value (mono-summed) or per-channel pair? | Proposal: mono-summed single value for v1. |
| UX-Q6 | Stale-reference terminology & UI treatment after metric change | Proposal: reference row shows "stale — re-read", Adjust disabled. |

## 8. Non-goals (v1)

- No sidechain/dual-instance reference mode.
- No per-frequency (spectral) compensation.
- No loudness targeting for delivery/mastering (-14 LUFS style target
  matching).
- No installers/packaging in v0 (FR-18).
- No presets system beyond a saved default state (may be revisited).
