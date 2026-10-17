# MakeGain — Technical Specification

> Version: v0.1 — 2026-10-17
> Source of truth: [../functional_reqs/MakeGain_FR-v1.md](../functional_reqs/MakeGain_FR-v1.md) (frozen)
> Convention: each section maps to the FR it implements. Items marked
> **[assumed]** are tech decisions made by the assistant pending owner veto.

---

## 1. Language & toolchain

| Item | Decision | Notes |
|---|---|---|
| Language | **C++20** **[assumed]** | JUCE 8 minimum is C++17; we stand on C++20 for `std::span`, ranges-light code, better atomics ergonomics. No exotic feature creep. |
| Framework | **JUCE 8** (pinned tag at M0) | Chosen in design phase: same codebase → VST3 + CLAP + AU; mature macOS/Linux arm64. |
| Build system | **CMake ≥ 3.24 + Ninja** | Presets per platform (§6). |
| C++ formatter | clang-format **[assumed]** | Config committed at M0; astyle/indent not used. |
| Plugin-UI toolkit | JUCE component system (no OpenGL first) | De-risk Asahi/Mesa GPU quirks; software-drawn editor v1. |
| Linux sound APIs | none directly used | Plugin runs in host process; host talks to PipeWire/CoreAudio, not us. JUCE's unconditional ALSA dep applies to standalone targets only — we build none of those; headers still needed at config time on Linux. |
| Compilers | macOS: Xcode clang; Linux: gcc (as installed by Fedora Asahi) | clang on Linux optional; both should work, CI pins one. |

## 2. Licensing & third-party deps (SBOM basis)

- **MakeGain licence: AGPLv3** **[assumed]** — direct consequence of JUCE's
  AGPLv3 option; repo stays public/pure. Every source file carries the SPDX
  header from M0. If ownership of JUCE commercial licence ever happens, this
  decision is re-openable (FR-19 note).
- **Third-party inventory** (initial SBOM; maintained in this file's table +
  `DEPENDENCIES.md` generated at M0):

| Dependency | Version policy | Why | Licence effect on MakeGain |
|---|---|---|---|
| JUCE | pinned tag, bump manually | framework | AGPLv3 (or GPLv3) propagation |
| clap-juce-extensions | pinned commit | adds CLAP target from JUCE 8 | MIT (safe) |
| Catch2 | pinned tag, fetch on demand | DSP unit tests | BSL-1.0 (safe) |

- No other runtime dependencies. DSP code is written in-repo (`src/dsp`),
  not pulled from a packager — keeps SBOM at 3 deps.
- **SBOM generation**: scripts produce `sbom.spdx.json` listing the above
  three + MakeGain itself; CI refreshes it on releases. (Full
  dependency-graph tooling (syft/grype) deferred until there's a release
  artifact to scan.)

## 3. DSP specification (maps to FR-2..FR-11)

### 3.1 Signal measurement kernel (shared by Read and Adjust)

Rate-agnostic measurement chain, one shared implementation, instantiated
twice (analysis-in path). Mono logic operates on the mono-sum of L+R (UX-Q5).

1. **K-weighting** (ITU-R BS.1770-4): two-stage biquad (shelf + high-pass),
   coefficients interpolated per sample-rate (44.1/48 * base design rate
   48 k; scale factor applied for rate changes).
   → used by the *LUFS* metric only.
2. **RMS metric**: plain mean-square accumulator; dB conversion at the end.
3. **Block integration**: 400 ms sliding windows, 100 ms hop. One window per
   metric, culminating in short-term LUFS (gated) or RMS dB.
4. **Gating (LUFS only)**: absolute −70 LUFS + relative −10 LU relative
   gates per BS.1770-4, applied over the training window during Read.
5. **Outputs** (both metrics computed at all times on the same analysis
   stream, so FR-6a holds with zero extra pass): `lufs` (LU), `rms_dB`.

### 3.2 Read / Reference capture (FR-2, FR-4, FR-6a, FR-7, FR-5a)

- `trainWindowSec` — code constant namespace, default **3.0 s** (a single
  var, not a written UI param; still compile-time readable, future-friendly).
- Pressing Read resets the accumulators, measures hard for exactly
  `trainWindowSec`, then auto-completes and writes the reference record
  `{ lufs, rms_dB, windowSec, sampleRate }` atomically.
- Read passes audio through **untouched** (gain stage bypassed in float64
  domain, no clicks).
- Low-confidence flag (FR-5a): if input RMS stayed < −70 dBFS-ish absolute
  gate for the whole window, set `lowConfidence = true` for the UI flag.

### 3.3 Adjust / Gain computer (FR-3, FR-8, FR-10, UX-Q3 hold)

Loop every process block, on the analysis path (not the audio path):

```
delta = ref[selected] − measure[selected]          // dB
targetGain = clamp(−delta, ±MaxComp)
if outputRMS < silenceAbsGate for > silenceHoldSec (5 s [assumed]):
    targetGain = lastGain                          // UX-Q3: hold
gain ← slew(targetGain)                            // FR-3 ramp
```

- **Slew**: exponential one-pole toward target; time constant from the
  *ramp speed* parameter (default 300 ms [assumed]); per-sample coefficient
  interpolation = no zipper; gain change is linear-in-dB during slew.
- **Clamp (FR-10)**: ± MaxComp (default **24 dB**). Flag if clamped this
  block.
- Metric switch (FR-8): pure reads of two already-computed values + two
  already-stored references; no recomputation on switch.
- Automation of *Active* (FR-15): ramped on/off through the same slew path —
  bypass is never a hard click.

### 3.4 Realtime discipline (FR-20, FR-21)

- `processBlock` is allocation-free, lock-free: parameters read from
  `std::atomic<float>`; reference record swapped via
  `std::atomic<std::shared_ptr<Reference>>` (or double-buffered struct —
  decide in code, **[assumed]** atomic-swap pointer is the plan).
- No DSP work happens on the message thread other than enqueueing pushes.
- Determinism (FR-21): the measurement state is carried in the plugin
  instance, not in host-specific objects; no time-of-day or wall-clock reads
  in DSP code.

### 3.5 State & persistence (FR-7, FR-22, FR-12)

- All parameters via `AudioProcessorValueTreeState`.
- Reference record serialized as extra APVTS state (binary block). Loading a
  session restores full mode + references.

## 4. Plugin architecture

```
src/
├── dsp/                 # no JUCE includes; pure C++20, unit-testable standalone
│   ├── kweighting.{h,cpp}
│   ├── loudness_meter.{h,cpp}     # chain from 3.1, produces lufs + rms_dB
│   ├── gain_computer.{h,cpp}      # 3.3 state machine
│   └── reference.h                # {lufs, rms, windowSec, sampleRate} POD
├── plugin/
│   ├── makegain_processor.{h,cpp} # AudioProcessor; buses: in stereo, out stereo
│   ├── parameters.{h,cpp}         # APVTS layout table (single source of IDs)
│   └── state.h                    # serialize/deserialize reference record
└── ui/
    ├── editor.{h,cpp}             # components per FR-12..15, stock JUCE widgets
    └── lookandfeel.{h,cpp}        # simple dark theme [assumed], no deps
```

- **Buses**: in/out = stereo (+ optional mono layouts). **No sidechain bus**
  (read→adjust design needs none; keeps GarageBand path identical).
- **Channel handling (v1)**: analysis on mono-sum L+R; gain applied
  identically to both channels.

## 5. Formats & hosts (from FR-16, FR-17)

| Target | Standard (v1 ships) |
|---|---|
| VST3 | yes (primary; Reaper, Ardour) |
| CLAP | yes (via clap-juce-extensions; Reaper) |
| AU (v2) | yes (exercise tier; GarageBand; same source) |
| Standalone | no |
| LV2 / AAX | no |

## 6. Build matrix & presets

`CMakePresets.json`:

| Preset | Platform | Arch |
|---|---|---|
| `macos` | macOS 26/27, clang | arm64 (+universal on request) |
| `linux-arm64` | Fedora Asahi gcc/clang | aarch64 |
| `linux-x64` | generic Linux | x86_64 |

- Outputs per build: `MakeGain.vst3`, `MakeGain.clap`, `MakeGain.component`
  (mac only).
- No code signing beyond ad-hoc on macOS; Gatekeeper-on-first-launch
  documented in the user-facing build guide (not blocking v0).
- CI (later milestone, not v0 — FR-18): GitHub Actions matrix; GHA free
  arm64 Linux runners verified at that point.

## 7. Testing strategy (FR-20, FR-21 quality gates)

- `tests/dsp_*`: Catch2 host-side tests (no JUCE needed — dsp/ is JUCE-free):
  K-filter response vs published BS.1770 curves; loudness meter golden-signal
  (canned WAV → expected LUFS/RMS); clamp/slew/hold behavior; metric-switch
  zero-op property.
- `tests/plugin_*`: APVTS round-trip (state save/load → FR-22).
- Manual host matrix per milestone (Reaper macOS / Reaper Asahi / Ardour /
  GarageBand) — gates M1..M3 exits, recorded in PR descriptions.

## 8. Risks carried into tech phase

| Risk | Handling |
|---|---|
| clap-juce-extensions vs current JUCE 8 tags drift | pin both in lockstep; smoke-build all 3 formats in CI-lite script |
| Asahi aarch64 codegen quirks | never ship arch-specific intrinsics (SSE/NEON hand-written); JUCE SIMDRegister ok; plain float path is the reference implementation first, SIMD only behind an abstraction if profiling demands |
| JUCE AGPLv3 expectations | GPL-compliance page in this repo before v0 release tag |
| Realtime bugs in Linux-only paths | unit tests cover all DSP; host-matrix exercise covers plugin-level paths |

## 9. Open (veto-list) — decisions [assumed] in this doc

1. C++20 vs C++17 (§1) — any objection?
2. AGPLv3 as the project licence (§2).
3. Silence-hold window 5 s (§3.3) and ramp speed default 300 ms.
4. Mono-sum analysis for v1 (§4), stereo-reference in later milestone (UX-Q5).
5. Repo internal `dsp/` purity rule (no JUCE includes) — it buys cheap unit
   tests; costs a thin adapter layer in plugin/.
