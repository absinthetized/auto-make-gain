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
| Compilers | macOS: Xcode clang (fixed by the platform); Linux: **gcc primary** (Fedora Asahi default), clang as CI-diversity job later | Rationale: distro-native toolchain for the FR-18 "clone+build" model; JUCE's Linux known-good path is gcc; clang adds value as a *second* opinion in CI (different warnings) and unlocks libFuzzer (`-fsanitize=fuzzer` is clang-only) once CI exists. |

## 2. Licensing & third-party deps (SBOM basis)

- **MakeGain licence: AGPLv3** — **approved by owner 2026-10-17** (FSF
  copyleft is the preference). Repo stays public/pure. Every source file
  carries the SPDX header from M0. If ownership of a JUCE commercial
  licence ever happens, this decision is re-openable (FR-19 note).
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
if outputRMS < silenceAbsGate for > silenceHoldSec (5 s — approved by
    owner 2026-10-17):
    targetGain = lastGain                          // UX-Q3: hold
gain ← slew(targetGain)                            // FR-3 ramp
```

- **Slew**: exponential one-pole toward target; time constant from the
  *ramp speed* parameter (default 300 ms — approved by owner 2026-10-17);
  per-sample coefficient interpolation = no zipper; gain change is
  linear-in-dB during slew.
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

### 1.1 Framework decision record (why JUCE — alternatives checked)

| Candidate | Formats | macOS arm64 | Linux aarch64 | UI toolkit | Licence | Verdict |
|---|---|---|---|---|---|---|
| **JUCE 8** | VST3, CLAP (via clap-juce-extensions), AU (v2/v3), AAX, LV2, standalone | mature | mature | full component system, resizable | AGPLv3 / GPLv3 / commercial | **Selected** |
| DPF (DISTRHO) | LADSPA, LV2, VST2, VST3, CLAP, JACK standalone | buildable | excellent (its home turf) | modest, smaller widget set | LGPL | Rejected: **no AU target** → fails GarageBand exercise (FR-16) |
| iPlug2 | VST2/3, AU, AUv3, AAX, CLAP, WebAssembly | works | exists, less battle-tested | decent | permissive (WDL/IPL-style) | Strong runner-up; rejected for this project: smaller community proof on Linux arm64 plugin hosting, and permissive licence buys us nothing since owner *prefers* copyleft |
| Raw CLAP SDK only (+ hand UI) | CLAP only | yes | yes | you write it | MIT | Rejected: AU + VST3两手 reinvention for a solo project |
| LV2-first | LV2 only | n/a (mac hosts don't speak it) | yes | n/a | — | Rejected: fails macOS |

Decision axes (from the FRs): AU must exist (exercise tier, FR-16) → only
JUCE and iPlug2 remain → owner prefers FSF-copyleft and JUCE's AGPLv3 route
matches that → JUCE. The one JUCE cost (copyleft propagation) is a feature
per owner's stated preference.

**C++ standard note (veto-item 1 resolved):** JUCE 8's official minimum is
C++17. C++20 is a compatible superset we compile with; nothing in JUCE
blocks it. Rule: features we use must stay within "compiles under both
C++17 and C++20"; any C++20-only construct needs a code comment. Confirmed
not to depend on JUCE internals that differ between standards.

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

- **Sanitizers (path-A memory-safety strategy, owner-approved 2026-10-17):**
  all `tests/` targets get a `-sanitize` CMake variant compiling with
  `-fsanitize=address,undefined` (GCC on Asahi, clang on macOS), wired into
  CI so the whole Catch2 suite runs under both ASan (incl. LeakSanitizer)
  and UBSan on each push. Initial Sanitizer lane = the gcc Linux preset
  (Asahi-dist native); a clang lane (and libFuzzer) joins with CI
  enablement in the backlog. Plugin builds stay sanitizer-free; a
  documented "sanitized plugin" preset exists for manual host repro
  sessions.
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

## 9. Decision log (veto items status)

1. **C++ standard** — resolved: compile as C++20 with C++17-compatible
   features only (JUCE 8 baseline is C++17; see §1.1).
2. **AGPLv3** — **approved by owner** (FSF-copyleft preference).
3. **Rephrased:** two tunable numeric defaults live in the gain computer:
   ① *silence-hold timer* = how long the output must stay silent before the
   plugin stops chasing and simply freezes its last applied gain → default
   **5 s**; ② *ramp speed* = how fast the applied gain glides toward a new
   target (one-pole time constant) → default **300 ms** (fast enough to be
   invisible, slow enough to never click). Both will be code constants with
   UI exposure later. **Approved by owner 2026-10-17.**
4. **Mono-sum analysis for v1** — **approved by owner** (stereo dual-ref is
   a future milestone).
5. **Purity rule, restated by owner:** the *actual* requirement is platform
   isolation — anything leaking arch/OS details (SIMD intrinsics, endian,
   thread primitives, file system) must be fenced behind thin abstraction
   layers. The concrete form (pure-dsp vs JUCE-free, language features,
   arch guards) gets finalized when §1's language decision is closed.

## 10. Session backlog (this project's live backlog)

- **Release & branching model** — not yet discussed: branching strategy,
  tags (semver?), release cadence, `main` protections, version string
  surfaced in the plugin UI/about.
- Installers / packaging (bumped from v0 scope, FR-18).
- Noise/silence gate *skipping* in computation (deferred idea, FR doc §9).
- CI enablement (deferred from v0 by FR-18; presets first).

### Decision log — language vote, closed 2026-10-17
- **Path A confirmed by owner**: pure C++20 + JUCE for v1.
- Rejected: **Path B** (JUCE C++ shell + Rust DSP core via `cxx` — two
  toolchains, FFI surface disproportionate to our small DSP scope), and
  **Path C** (full-Rust `nih-plug` — memory-safe, but drops the AU target
  and rewrites the framework decision record for little real gain given
  sanitizer coverage).
- Sanitizers (ASan + UBSan on every CI test run) are the official,
  owner-adopted memory-safety strategy (§7). Any future DSP growth that
  reopens the Rust question will cite this entry as recorded precedent.

