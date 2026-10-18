# M1 plan — "the kernel earns its meters"

Status: proposed 2026-10-18. Frozen FR contract: `functional_reqs/FR-v1.md`.
Architectural rules: `tech_specs/TS-v0.1.md` §7 (`src/dsp/` JUCE-free; plain
float path first; no hand-written SIMD; latency-neutral plugin).

## Goal (M1 exit)

M1 converts the passthrough stub into a **functional auto-gainer
instrument**: a real dual-metric loudness kernel (LUFS + dB RMS), a gain
computer honoring every behavioral FR (FR-1..FR-11 verbatim), real meters,
state labels, and parameter persistence — verified on the full host
matrix, including the GarageBand AU exercise that gates this milestone
and the parked Ardour-CLAP check (build.md §6.10).

## Architecture

Three pure-C++ units in `src/dsp/`, each independently testable without
JUCE or a host:

```
src/dsp/k_weighting.{h,cpp}      BS.1770 K-filter (2-stage cascade), sr-adapted
src/dsp/loudness_meter.{h,cpp}   dual-metric kernel: LUFS short-term + dB RMS,
                                 continuously fed; output = snapshot struct
src/dsp/gain_computer.{h,cpp}    state machine IDLE/READ/ADJUST + g = ref−meas,
                                 clamp (±MaxComp, default 24 dB), slew (300 ms),
                                 silence-hold (5 s), both refs stored (FR-6a/7)
```

Wiring stays thin: the processor owns instances and drives the state
machine from host events; snapshots cross to the UI atomically (never a
lock in `processBlock`); APVTS carries metric choice + MaxComp; **Active
is the only automatable parameter** (FR-20); the Train button is a
host-pressed action, not a parameter. Mono-sum everywhere (UX-Q5).

## Two-PR milestone split (respects the ≤5–7 source files metric)

M1's honest file count would be ~12–13; the natural seam (kernel vs.
compute+UI) splits it into two sequential PRs.

## Red/green discipline (owner-mandated, 2026-10-18)

No DSP unit's implementation may exist before its tests. For each unit:
1. **RED commit** — tests + a *stub* implementation (compiles, obviously
   wrong: identity filter / zero output / no gain). The checkpoint script
   is expected to FAIL; the failing output is quoted in the commit
   message. The owner eyeballs the assertions *before any real
   implementation exists*, so tests cannot be reverse-engineered to
   flatter the code.
2. **GREEN commit** — the real implementation; the same tests now pass
   (ALL GREEN in the checkpoint output, quoted in the message).

A red commit deliberately does not build-pass. Acceptable: there is no CI
yet (the local `check-all.sh` is the gate), and bisectability is
preserved *across* green commits.

### PR-A `m1-mvp` — the loudness kernel (~7 commits, 7 files)

| # | Commit | Files | Checkpoint |
|---|---|---|---|
| C1 | `docs: m1 strategy & plan` | this file | owner approves the plan itself |
| C2 | `tests(red): K-weighting curves + stub` | `src/dsp/k_weighting.{h,cpp}` (stub), `tests/dsp/k_weighting_test.cpp` | `check-all.sh` FAIL quoted in message |
| C3 | `dsp(green): K-weighting filter (BS.1770, JUCE-free)` | same 3 files | ALL GREEN quoted |
| C4 | `tests(red): loudness golden signals + switch property + stub` | `src/dsp/loudness_meter.{h,cpp}` (stub), `tests/dsp/loudness_meter_test.cpp` | FAIL quoted |
| C5 | `dsp(green): dual-metric loudness kernel` | same files | ALL GREEN quoted |

PR-A's host-facing behavior is still passthrough (the kernel is built but
not wired) — one mac Reaper load-check confirms nothing regressed.

### PR-B `m1-gain-compute-ui` — the compensate machine (~7 commits, ~6 files)

| # | Commit | Files |
|---|---|---|
| D1 | `tests(red): gain-computer behavior + stub` | `src/dsp/gain_computer.{h,cpp}` (stub), `tests/dsp/gain_computer_test.cpp` |
| D2 | `dsp(green): gain computer (state machine, clamp, slew, silence-hold)` | same files |
| D3 | `plugin: wire kernel + gain computer into processor (mono-sum)` | `src/plugin/MakeGainProcessor.{h,cpp}` |
| D4 | `plugin: APVTS state + persistence (FR-7 reference survives reload)` | same files |
| D5 | `ui: real meters + state labels (ref captured, clamped, no-signal, low-confidence)` | `src/ui/MakeGainEditor.{h,cpp}` |
| D6 | `test: APVTS round-trip` | `tests/state_test.cpp` |

### M1 exit gates (owner checkpoints, end of PR-B)

1. mac Reaper: compensation audibly works, meters sane.
2. Asahi: pen-transfer re-test (§8.1 lane).
3. **GarageBand AU exercise** (the tier that gates M1, TS §7).
4. Ardour re-test **including the parked CLAP path** (§6.10 checklist).

## Risks carried into M1

| Risk | Handling |
|---|---|
| BS.1770 accuracy at arbitrary sample rates | stage-level tests at 44.1/48/96 kHz; coefficients computed at runtime for the instance rate, never hardcoded per-rate tables |
| training completions racing block boundaries | the kernel owns time, not the audio callback: the window counts samples; completion flags are atomics; no locks in `processBlock` |
| editor repaint cost | UI receives an atomic snapshot struct, repaint is decoupled from metering |
| automation of Active while in ADJUST | FR-5 semantics: leaving to IDLE preserves the reference; re-engage never re-trains |

## Arch-coverage decision (owner, 2026-10-18)

Current test coverage: macOS arm64 (native) + Linux aarch64 (container
lane). Linux x86_64 has a CMake preset but is exercised nowhere yet.
Decision: close the amd64 gap as its **own PR during M1** —
`m1-x64-rosetta-lane` — extending `scripts/docker-check.sh` with an
`--x64` mode that builds the same fedora image with
`--platform linux/amd64` (Rosetta emulation on the owner's mac) and runs
the `linux-x64` preset + ctest inside. The lane is understood to be slow
(emulated) and therefore on-demand/pre-PR, not per-commit. This branch's
PR-A closes with the aarch64 verbatim evidence; amd64 evidence lands with
that lane. macOS x86_64 stays out of scope (no hardware; FR scope is
arm64-first).

## Out of scope for M1 (deferred, per FR/TS)

stereo/dual-mono metering (UX-Q5), perceptual gate thresholds (FR-14
deferral), stereo width, any SIMD work (TS §8 leaves SIMD behind an
abstraction for a profiling-evidence decision, not a policy one).
