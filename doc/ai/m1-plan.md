# M1 plan — "the kernel earns its meters"

> **Audience: AI/owner process material** — live milestone strategy +
> checkpoint/evidence ledgers. Humans: start at the README's docs map.

Status: proposed 2026-10-18. Frozen FR contract: `doc/human/MakeGain_FR-v1.md`.
Architectural rules: `doc/ai/MakeGain_TS-v0.1.md` §7 (`src/dsp/` JUCE-free; plain
float path first; no hand-written SIMD; latency-neutral plugin).

## Goal (M1 exit)

M1 converts the passthrough stub into a **functional auto-gainer
instrument**: a real dual-metric loudness kernel (LUFS + dB RMS), a gain
computer honoring every behavioral FR (FR-1..FR-11 verbatim), real meters,
state labels, and parameter persistence — verified on the full host
matrix, including the GarageBand AU exercise that gates this milestone
and the parked Ardour-CLAP check (build.md §6.10).

*2026-10-22, Amendment A2: this goal statement predates the A2
contract change — the GarageBand AU gate has retired and the gain
computer is re-scoped to the delta engine; see the re-scope section at
the bottom of this file, which is operative over the body above.*

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

**Verification record (2026-10-18):**

- macOS arm64 (Xcode clang): `All tests passed (26 assertions in 11 test
  cases)` per red/green commit messages (C2 red → C3 green → C4 red →
  C5 green).
- Linux aarch64 (fedora:44 container, gcc 16.2.1, glibc — different
  codegen + libm): both lanes — `linux-arm64` and
  `linux-arm64-sanitize` (ASan/UBSan/LSan) — configure + build + ctest
  `ALL GREEN` via `scripts/docker-check.sh`. The K-weighting golden
  assertions and loudness anchor (−0.691 calibration argument) hold
  across both architectures: margins absorb libm/codegen variance, and
  where certainty was needed the spec cites analytic values verified
  against the standard's own Table 1 coefficients.

### PR-B `m1-gain-compute-ui` — the compensate machine

**Commit-detail plan (drafted 2026-10-20, per AGENTS rule 4+8):**
design intent first: the FSM, ramp, hold, clamp and the reference-codec
live in a JUCE-free `GainComputer` (owns a `LoudnessMeter`), so the
entire compensate machine is unit-testable headless; the processor
becomes thin glue (buffers in, params in, buffered gain out); the codec
inside the same module keeps the source-file metric at the ceiling.

| # | Commit (rule 8 red/green where applies) | Files | Checkpoint (owner acts) |
|---|---|---|---|
| PB1a | `tests(red): gain-computer spec R1–R7 + codec round-trip + honest stub` | `src/dsp/gain_computer.{h,cpp}` (stub), `tests/dsp/gain_computer_test.cpp`, `CMakeLists.txt` | review the assertions; red output quoted in message |
| PB1b | `dsp(green): gain computer — FSM (IDLE/READ/ADJUST), clamp, one-pole ramp, silence-hold, reference codec` | same 3 files | — |
| PB2 | `plugin: wire GainComputer into processor — APVTS params, FR-7 persistence, mono-sum gain` | `src/plugin/MakeGainProcessor.{h,cpp}` | **mac Reaper mid-gate**: compensation audibly works, ramp is smooth, active-toggle + re-engage keep the reference (FR-5), session reload restores it (FR-7) |
| PB3 | `ui: meters + mode controls + status line flips to real DSP` | `src/ui/MakeGainEditor.{h,cpp}` | mac Reaper visual check: all FR-12 readouts lie live (in/out loudness, applied gain, reference, mode, metric), clamped/low-confidence flags show |
| PB4 | `docs: PR-B verification record + lane runs` | `doc/ai/m1-plan.md` | `docker-check.sh` (arm64) + `--x64` on this branch; then push/PR |

Gain-computer spec sketch for PB1a (tests assert, owner reviews):

- **R1** compensation math: `g_dB = ref[metric] − live[metric]`
  (metric = LUFS or dB RMS; both pair selectable at any time).
- **R2** clamp (FR-10): |g| > MaxComp → applied = ±MaxComp, `clamped`
  flag exposed.
- **R3** ramp: one-pole slew toward target with tc = ramp-speed param
  (default 300 ms, TS §9 item ③): deterministic closed-form check over
  N samples; no step larger than the slew bound; automation-safe.
- **R4** silence-hold (FR-9/UX-Q3): invalid snapshot or sustained silence
  → gain holds last applied value; state survives until a valid reading
  returns, then chases again.
- **R5** engage from IDLE (FR-5): starts at 0 dB and *ramps* to target —
  no slam, no re-train; IDLE return clears applied gain ramped, too.
- **R6** READ completes on its own after the 3.0 s code-constant window
  (FR-2/FR-4): captures BOTH metrics (FR-6a), flips low-confidence flag
  when the window was silent (FR-5a); re-read replaces reference at
  completion, never mid-run.
- **R7** reference codec round-trip (FR-7 substance, JUCE-free):
  encode→decode = identity; tagged version field; unknown trailing data
  is ignored, not fatal (forward compat).
- Ragged block sizes (7/11/13) across all cases — same
  accumulation-robustness pattern as PR-A.

File-net for PR-B after merge (metric check ≤5–7 source): +
`gain_computer.{h,cpp}`, + processor & editor touch-ups, `CMakeLists`
edit → **7 non-test source files** exactly; param IDs stay in the
processor header (no separate `parameters.{h,cpp}` at v1 — metric
pressure decision); test files don't count (PR-A precedent).

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

**Dismissed, recorded as future improvement (owner, 2026-10-19):**
adding Debian stable and/or Arch container lanes (via thin
`Dockerfile`s + `--distro` flag, exercising bootstrap.sh's currently
untested apt/pacman package branches and the "old gcc" end of the
compiler range). Dismissed for now: the project is not mature enough
to carry three distro × two-arch combinatorics — the payoff
(README minimum-compiler claims, package-list validation) is real but
belongs to the public-adoption phase, not the M1 build-out. Revisit
when CI exists; Debian-stable-only is the first cut if/when revived.

**x64 lane evidence record (2026-10-19, first full `--x64` pass,
branch `m1-x64-rosetta-lane`):**

- `linux-x64` lane: configure + build + ctest ALL GREEN under
  Rosetta-emulated amd64 (fedora:44, gcc 16.2.1-2 — same release and
  compiler build as the Asahi Remix's aarch64 userland, now proven on
  the x86_64 ABI). All 26 Catch2 assertions / 11 cases pass, including
  the K-weighting anchor (+0.691 Table-1 argument), the loudness
  golden signals (B1 −3.0 LUFS), and the B4 cross-tie — the sealed
  cross-arch evidence promised by the arch-coverage decision above.
- `linux-x64-sanitize` lane: first provisioning attempt exposed an
  environment gap — the link failed with `ld.bfd: cannot find
  /usr/lib64/libasan.so.8.0.0`: bootstrap.sh (the single dep source)
  never listed the sanitizer runtimes, and dnf's dependency resolution
  supplied them on aarch64 but not on x86_64. Fixed by installing
  `libasan` + `libubsan` explicitly (`d8c740b`); the re-run then
  linked and passed clean (`100% tests passed`), also under
  ASan/UBSan/LSan. Lesson recorded: lane-critical runtimes are
  explicit in bootstrap.sh, never by dependency luck.
- Standing caveat (build.md §8.2): this lane is emulated codegen/libm
  evidence, not a claim about real amd64 hardware or audio behavior;
  Tier 2 on bare metal remains the oracle for host behavior.

## Out of scope for M1 (deferred, per FR/TS)

stereo/dual-mono metering (UX-Q5), perceptual gate thresholds (FR-14
deferral), stereo width, any SIMD work (TS §8 leaves SIMD behind an
abstraction for a profiling-evidence decision, not a policy one).

## Re-scope after Amendment A2 (2026-10-22, operative)

The A2 contract change (`MakeGain_FR-v1.md` §10) retires the training
FSM mid-PR-B2 and converts the product to the internal-sidechain delta
engine. Of PR-B2's five landed commits (branch `m1-wire-up`):

- `6108e3e` docs post-mortem — stands
- `a076069` hardening + `21f554f`/`eabb35a` red/green trajectory — the
  **emitter and chunk-invariance mechanics survive into the new engine**;
  the training/reference semantics they pinned are retired with the FSM
- `f7920a3` stage-1 wiring — replaced by the aux-bus wiring below

**Remaining PR-B2 plan (lands at ~9 commits, within the 10-metric):**

| # | Commit | Files | Checkpoint |
|---|---|---|---|
| A2 | `docs: FR amendment A2 + decisions + M1 re-scope` | `MakeGain_FR-v1.md`, `decisions.md`, this file | owner eyeball (docs only) |
| PB2e-r | `tests(red): delta-engine spec R-D1..R-D9 + honest stub` — gain_computer rewritten to a stub; new tests for the delta engine (spec rows below) | `src/dsp/gain_computer.{h,cpp}` (in-place rewrite), `tests/dsp/gain_computer_test.cpp` | owner reviews assertions; red output quoted |
| PB2e-g | `dsp(green): delta engine — dual-meter AGC, hold, clamp, ramp, emitter` | same (+ TS delta-engine spec note in `MakeGain_TS-v0.1.md` §9) | ALL GREEN quoted |
| PB2f | `plugin: aux-bus wiring — second stereo input bus, wet/dry mono sums, Active-AGC` | `MakeGainProcessor.{h,cpp}`, `MakeGainEditor.{h,cpp}` (status line + 4 Hz refresh), no CMake change needed | **mac Reaper smoke A2**: TAP first in chain, Apply last; edit-compensation with program-dynamics pass-through; toggle; tap-loss fallback (silence the aux); **mono-track case with TapMono** (mono main = mono aux, pin 2) |

**PB2f smoke record (owner session 2026-10-22, mac Reaper arm64):**

- ✅ 2-track send setup (SOURCE no-FX → WET 1/2 + 3/4 pre-FX/pre-fader,
  SOURCE master send off): duck compensation works, dry-tap-missing flag
  proven during routing debug (aux feed absent → flag lit as specced).
- ✅ 1-track TAP setup (`MakeGainTap` as FIRST FX, MakeGain LAST, no
  sends): duck compensation works. Files: repo `jsfx/`, installed to the
  Reaper user effects folder.
- Finding, no action (owner): 1-track-without-TAP is impossible in
  stock Reaper (sends tap post-FX; self-feed is a feedback loop) — the
  aux-bus design + optional Tap covers 1-track workflows. Portability
  answer per host: generic aux bus everywhere + host-specific
  convenience (Reaper Tap; Ardour sidechain-from-track-input to be
  verified in the §6.10 gate).
- ⏳ Still open on the smoke: Active-toggle ramp-home, mono-track case,
  first-Activate-no-aux calm (spot-checked in routing debug: no-spike
  behavior held). Owner ruling: latency feel (ramp 300 ms / window 3 s)
  postponed — the PR-B3 Ramp parameter makes it tunable, defaults
  revisited only with proper heuristics.
| PB2g | `docs: PR-B2 verification record + lanes` | this file | `docker-check.sh` arm64 + `--x64` on the branch; then push / PR-B2 (description names the main-landing residue) |

**PR-B2 verification record (2026-10-22, branch tip `7388e06`):**

- Lane arm64 native: ALL GREEN (configure+build+ctest, main+sanitizer
  binaries) — evidence on the exact source bytes quoted in commits
  `e1a949b` (engine) and `8aade8e` (wiring).
- Lane amd64 (`--x64`, Rosetta-emulated): ALL GREEN same day, after the
  last code change (three docs/jsfx commits touched no C++ since).
- macOS native: built + installed (VST3/CLAP/AU Release, 22:11) — CLAP
  needed a manual folder copy (clap-juce-extensions does not
  auto-install it; note for the build.md sweep in PR-B3).
- Reaper smoke A2: ✅ both topologies (2-track sends, 1-track TAP).
- Commit-metric disclosure: branch is 12 commits (the plan budgeted
  ~9–10; the delta = early TAP commit + two owner-ruling docs commits).
  Squash-merge keeps the bisect story in the branch history.


**PR-B3 (follow-up PR, M1 continuation):** stage-2 parameters (Metric,
MaxComp default 16, ramp speed), the re-derived UI (FR-12 readouts,
`clamped` / `no_signal` / `dry tap missing` flags, status line), Reaper
smoke B/C, and the remaining M1 gate: **Ardour re-test incl. the parked
CLAP checklist (§6.10)**. *(The TAP JSFX files shipped early — PR-B2,
jsfx commit — because the owner's 1-track smoke needed them.)*

**Amended M1 exit gates (A2):** mac Reaper compensation + meters sane;
Asahi pen-transfer re-test; Ardour retest incl. CLAP. *(GarageBand AU
exercise retired by A2.)*

**PB3 gate (BLOCKING, owner bug report 2026-10-22 evening): −3 dB on the
engaged unity path.** Symptom: engaged → signal −3.0 dB; host bypass →
+3 back; repro with MakeGain the only FX in the chain. Analysis so far
(code re-read on `m1-unity-fix`): the apply path cannot attenuate at
applied = 0 dB (literal ×1.0; engine R-D1 pins identical-taps ⇒ delta
exactly 0), so the delta engine is being fed DIFFERENT tap content
while both meters read valid. Prime suspect: a nonzero HOLD — a stale
3/4 feed (old two-track send) went silent → freeze kept an old applied
value forever (hold semantics keep `target = applied` indefinitely);
owner's −3 dB ≈ an earlier real compensation frozen in. Investigation
plan (branch `m1-unity-fix`): (1) owner bisect answers — status line in
engaged vs bypass, fresh-track no-TAP/no-sends repro, topology used;
(2) depending on answers: either a hold-liveness rule (option to drop
to 0 dB when a tap goes missing? owner decision — anti-chase vs
stuck-gain) or a wiring-only fix. No param spec red until unity path
is clean.

**RESOLUTION (2026-10-23):** root cause found — the TAP JSFX wrote
`spl3/spl4`; JSFX sample registers are 0-BASED, so that is lanes 4/5,
and the aux lanes 3/4 read (silence, wet-L) = half power ⇒ dry tap
−3 dB ⇒ the engine dutifully compensated −3. The plugin was right;
the copied content was offset one lane. Fix:
`spl2 = spl0; spl3 = spl1` (the mono variant was already correct;
`spl1 = spl0`). Owner re-test after the fix: −3 dB gone, transparent
on engage; pin-UI confusion during the test was a window-resize
artifact, not code. Lesson recorded: `tracking` can mask a dead aux
lane while the fold reads "valid + half-power" — deferred hardening
candidate: a status hint on aux-lane asymmetry (display only, never
auto-act).
**Delta-engine spec rows (pin targets for PB2e-r; owner reviews):**


- **R-D1** null invariant: dry == wet (identical streams) → target ≈ 0,
  applied stays 0 while engaged.
- **R-D2** program dynamics pass: both taps scaled by the same factor
  (e.g. +3 dB) mid-engagement → target unchanged, applied unchanged.
- **R-D3** edit compensation: wet drops −6 vs dry → target = +6, ramped;
  wet boosted +6 → target = −6.
- **R-D4** metric pairing: LUFS and dB RMS deltas are computed from the
  same slot windows; switching metric re-targets at the next slot from
  the still-running meters (nothing to re-capture).
- **R-D5** clamp: |target| > MaxComp → applied at ±MaxComp, `clamped`
  flag exposed (default 16 dB per A2).
- **R-D6** ramp: engage from 0 dB, one-pole toward target (tc =
  ramp speed, default 300 ms); disengage ramps to 0; no slam anywhere.
- **R-D7** hold: EITHER tap's newest slot below the wall-silence floor →
  freeze at last applied; resume on validity. `no_signal` streak (50
  slots) flags wet silence ≥ 5 s; aux silence ≥ 5 s flags
  `dry tap missing`.
- **R-D8** chunk invariance: identical trajectories at every sample
  across ragged/huge/zero chunk sizes (property carried from the
  PB2a/B5 machinery — emitter now observes the AGC's applied gain).
- **R-D9** state: no reference object, no codec, no training counters —
  persistence is parameters only (FR-22 narrowed).
