// src/dsp/gain_computer.h — the compensate machine (FR-3): compares the
// live measurement to the stored reference and produces the applied gain,
// with the clamp, the ramp, the silence-hold and the training state
// machine — all JUCE-free, pure C++17 (TS §7), so the full state machine
// is unit-testable headless (the host-facing processor is thin glue).
//
// MakeGain DSP layer behaviors sourced here:
//   FR-2/FR-4  READ (training): pressing to READ starts a training run;
//              feed audio and the run completes ON ITS OWN after the
//              fixed 3.0 s code window and captures BOTH metrics over
//              that same window (FR-6a). Completing a run always leaves
//              the machine IDLE (FR-2: the plugin stays un-engaged until
//              the user engages Adjust); a re-read replaces the previous
//              reference only at completion, never mid-run (FR-4).
//   FR-3       ADJUST: applied gain target = ref[metric] − live[metric].
//   FR-5       IDLE ↔ ADJUST engage: leaving IDLE ramps from the current
//              applied gain (0 dB from fresh IDLE) — never a hard slam;
//              entering IDLE ramps back to 0 dB the same way.
//   FR-8       metric switch re-targets instantly from the SAME stored
//              reference and the SAME live window — no re-measure, and
//              the reference is untouched by the switch.
//   FR-9/TS §9 ③  silence-hold: while no valid signal exists in the
//              window (silence or below the 3.0 s-freshness floor), the
//              target freezes at the last applied gain and the machine
//              stops chasing; a valid reading resumes the chase.
//   FR-10      clamp: |target| beyond MaxComp dB is applied AT MaxComp
//              and the clamped flag is exposed (FR-12 shows it).
//   FR-11      latency-neutral: gain is applied per-sample, no lookahead,
//              no delay line.
//   FR-7       reference codec: the reference record survives save/load
//              as bytes, version-tagged, forward-compatible (a decoder
//              ignores trailing bytes it does not know), all-or-nothing
//              (a failed decode leaves the stored reference untouched).
//
// Realtime contract (the processor calls process() on the audio thread):
//   no allocation, no locks, no I/O, no wall-clock reads. All setters are
//   for non-audio time (parameter changes brought over atomics by the
//   processor's glue — this module does not sync threads itself).
//
// Code constants (FR-2: the window length is NOT a UI setting; TS §9 ③:
// ramp speed is a code-exposed default until UI exposure is approved)

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "loudness_meter.h"

namespace makegain::dsp {

// Which metric pair drives the compensation target (FR-8: switchable at
// any time; ReferenceRecord always carries both).
enum class GainMetric { lufs, db_rms };

// The three machine states. `reading` is passthrough; `idle` is
// passthrough with the applied gain already ramped to 0 dB; `adjusting`
// applies gain.
enum class GainMode { idle, reading, adjusting };

// The reference record: both metric readings from the SAME 3.0 s window,
// captured at training completion (FR-6a).
struct ReferenceRecord {
  double lufs = 0.0;   // −inf means "the window was silence"
  double db_rms = 0.0;
  bool valid = false;  // false until a training run has completed
};

// Flags the UI shows (FR-12): clamped is live while ADJUSTing (last
// computed target exceeded the clamp); low_confidence sticks for the
// current reference (captured from a silent window — re-read to fix);
// no_signal lights on sustained wall-side silence.
struct GainFlags {
  bool clamped = false;
  bool low_confidence = false;
  // FR-12/UX-Q3: "no signal" — the input wall went (near-)digitally
  // silent and STAYED silent for the full 5.0 s code-constant grace
  // (TS §9 ③). The machine freezes its chase on the FIRST silent slot;
  // the flag timing is purely the UI-facing surface.
  bool no_signal = false;
};

class GainComputer {
 public:
  // `sample_rate` is fixed for the machine's life (a plugin instance
  // re-creates it on rate changes, like the meter it owns).
  explicit GainComputer(double sample_rate);

  // -- non-audio-time setters ------------------------------------------
  // FR-2/FR-4: idle→reading or adjusting→reading starts a fresh training
  // run (the old reference stays valid until the new run completes).
  // reading→idle or reading→adjusting ABORTS the run (keeping the old
  // reference if any, rejecting whatever half-window it had).
  void set_mode(GainMode mode);
  // FR-8: instant re-target from the same reference + live window.
  void set_metric(GainMetric metric);
  void set_max_comp_db(double db);   // FR-10 clamp bound
  void set_ramp_tc(double seconds);  // one-pole time constant of the ramp

  // -- audio thread -----------------------------------------------------
  // Feed mono audio and advance the machine over `count` samples. Any
  // block size (ragged, huge, zero) is safe; per-sample ramp math is
  // identical regardless of blocking (asserted by tests).
  void process(const float* samples, std::size_t count);

  // Sample-indexed variant (PB2a): same advance, PLUS one written output
  // per sample — out_gain_db[i] = the applied gain (dB) in force for
  // sample i, after the ramp step for that sample. The caller owns the
  // array (exactly `count` slots); the machine writes nothing besides
  // those values: allocation-free, no locks, purely observational (the
  // machine's own state advances identically with our without the out
  // array). Values are what the host-facing processor multiplies the
  // sample by: 10^(out/20).
  void process(const float* samples, std::size_t count,
               float* out_gain_db);

  // -- reads (cheap, any thread) ----------------------------------------
  GainMode mode() const;
  GainMetric metric() const;
  ReferenceRecord reference() const;
  GainFlags flags() const;
  // Current applied gain in dB (0.0 = unity, negative = attenuation).
  // During reading/idle this is whatever the ramp has decayed to.
  double applied_gain_db() const;

  // -- FR-7 codec: reference record <-> bytes ---------------------------
  // Returns an 18-byte, version-tagged, little-endian record; empty if
  // no valid reference exists. Decode: false on any problem (bad tag,
  // truncated, oversized version), true otherwise; trailing bytes are
  // ignored (forward compatibility), and a false return guarantees the
  // stored reference is unchanged. Byte order is assembled with shifts
  // (endian-clean — the DSP purity rule, TS §9 ⑤), bits via memcpy.
  // Test note: the round-trip test is same-host; cross-endian
  // correctness is by construction, not executed.
  std::vector<std::uint8_t> encode_reference() const;
  bool decode_reference(const std::uint8_t* data, std::size_t size);

  // Code constants (FR-2: window length is a code constant; TS §9 ③:
  // ramp + clamp defaults live in code until UI exposure is approved).
  static constexpr double kTrainingWindowSec = 3.0;
  static constexpr double kDefaultRampTcSec = 0.3;
  static constexpr double kDefaultMaxCompDb = 24.0;
  // Anything at or below this LUFS is treated as "the window is silence"
  // (drives silence-hold and the low-confidence flag).
  static constexpr double kSilenceLufs = -140.0;
  // Wall-side silence: newest 100 ms mean square ≤ this ⇒ digitally
  // silent input (≈ −120 dB FS). Drives the freeze (FR-9) and the
  // no_signal streak (5.0 s = 50 slots, TS §9 ③).
  static constexpr double kSilenceMeanSq = 1e-12;
  static constexpr std::size_t kNoSignalSlots = 50;
  // Codec version tag (first byte of every record). The tests reference
  // THIS constant — never a literal — so a bump fails the test linkage
  // instead of silently re-decoding old sessions. Version 0 is reserved
  // and NEVER legal (decode rejects it on sight), which gives the junk
  // case an all-future-proof invalid byte to test with.
  static constexpr std::uint8_t kCodecVersion = 1;

 private:
  LoudnessMeter meter_;  // shared by READ capture and ADJUST measurement

  GainMode mode_ = GainMode::idle;
  GainMetric metric_ = GainMetric::lufs;

  double sample_rate_ = 0.0;
  double max_comp_db_ = kDefaultMaxCompDb;
  double ramp_tc_ = kDefaultRampTcSec;

  ReferenceRecord reference_;
  GainFlags flags_;

  double applied_db_ = 0.0;  // the ramped applied gain
  double target_db_ = 0.0;   // what the ramp is currently chasing

  // Implementation state (GREEN commit — private slots only; the public
  // contract was already final at red): the cached one-pole α, the
  // internal 100 ms evaluation-slot clock, and the training-run counter.
  double alpha_ = 0.0;
  std::uint64_t eval_slot_samples_ = 0;
  std::uint64_t slot_fed_ = 0;
  std::size_t run_slots_ = 0;

  // Wall-side silence detection (FR-9): raw squares of the slot being
  // fed, the verdict at each boundary, and the silence-slot streak.
  double slot_sq_ = 0.0;
  bool tail_silent_ = false;
  std::size_t silent_streak_ = 0;

  // Target evaluation, exactly once per 100 ms slot boundary.
  void evaluate();
  // Per-sample one-pole advance of the applied gain toward the target;
  // when `out` is set, each post-step value is observed (PB2a green).
  void advance_ramp(std::size_t count, float* out = nullptr);
  // Raw sample energy of a chunk (the wall-side silence detector).
  static double slot_energy(const float* samples, std::size_t count);
};

}  // namespace makegain::dsp
