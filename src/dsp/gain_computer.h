// src/dsp/gain_computer.h — the A2 delta engine ("gain computer") (FR §10):
// continuously nulls the loudness delta the user's FX chain introduces
// between two measurement taps:
//
//   wet  = the chain-processed signal at this plugin's main input
//   dry  = the pre-chain tap, arriving on the aux/sidechain input
//
// applied gain target = −(loudness(wet) − loudness(dry)), evaluated on
// internal 100 ms slots, per selected metric, ramped per-sample toward
// the slot target (one-pole, FR-5's smooth engage/disengage), clamped
// to ±MaxComp. Program dynamics reach BOTH taps — equal taps mean zero
// delta, so the plugin does not chase the music; chain EDITS move the
// wet tap only, and the gain nulls them. There is no training: any
// reference/FSM machinery from the M1 leveler era is retired (A2).
//
// MakeGain DSP layer behaviors sourced here (A2 contract):
//   §10.2   target = −(wet − dry); slot-evaluated (chunk invariance)
//   §10.3   engage/bypass: ramp from the current applied gain, no slam
//   §10.4   either tap (near-)silent → hold last applied gain;
//           sustained wet silence ≥ 5 s → no_signal flag; sustained
//           aux silence ≥ 5 s → dry_tap_missing flag (the plugin does
//           NOT claim to distinguish an unconnected tap from genuinely
//           silent music — in both cases gain holds)
//   FR-10   clamp: |target| beyond MaxComp is applied AT MaxComp; the
//           clamped flag is exposed (FR §10.4/§10.6)
//   FR-11   latency-neutral: per-sample gain application, no lookahead
//
// Realtime contract (the processor calls process() on the audio thread):
//   no allocation, no locks, no I/O, no wall-clock reads. Setters are
//   for non-audio time; the processor's glue crosses parameter changes
//   over atomics — this module does not sync threads itself.
//
// Persistence note (FR-22, narrowed by A2): the machine holds NO session
// state of its own — no reference object, no codec, no training counters.
// What persists with the session is the parameter set only.

#pragma once

#include <cstddef>
#include <cstdint>

#include "loudness_meter.h"

namespace makegain::dsp {

// Which metric pair drives the chain delta (FR-6/FR-8 kept, A2 form);
// both meters always run, so a switch re-targets at the next slot from
// the same windows — nothing to re-capture.
enum class GainMetric { lufs, db_rms };

// Flags the UI shows (FR §10.6): clamped is live while the target hits
// the MaxComp bound; no_signal is sustained WET silence (≥ 5 s);
// dry_tap_missing is sustained AUX silence (≥ 5 s) — the honest label
// for "the dry tap did not feed me", which cannot be told apart from
// genuinely silent source audio.
struct GainFlags {
  bool clamped = false;
  bool no_signal_wet = false;
  bool dry_tap_missing = false;
};

class GainComputer {
 public:
  // `sample_rate` is fixed for the engine's life (a plugin instance
  // re-creates one on rate changes, like the meters it owns).
  explicit GainComputer(double sample_rate);

  // -- non-audio-time setters ------------------------------------------
  void set_metric(GainMetric metric);  // targets again at the next slot
  void set_max_comp_db(double db);     // FR-10 clamp bound
  void set_ramp_tc(double seconds);    // one-pole time constant of the ramp
  // Active toggle (A2 §10.3): engaged = delta-nulling; idle = ramp home
  // to 0 dB from wherever the applied gain currently is.
  void set_engaged(bool engaged);

  // -- audio thread -----------------------------------------------------
  // Feed BOTH mono sums (wet, dry) and advance over `count` samples; any
  // block size (ragged, huge, zero) is safe; the per-sample ramp math is
  // identical regardless of blocking (pinned by the R-D8 test). When
  // out_gain_db is set, it receives the applied gain in dB AFTER each
  // sample's ramp step — the values the processor multiplies both
  // channels by (10^(dB/20), FR-11). Caller owns `count` slots; writing
  // them is purely observational (the engine advances identically
  // without the array).
  void process(const float* wet, const float* dry, std::size_t count,
               float* out_gain_db);

  // -- reads (cheap, any thread) ----------------------------------------
  GainMetric metric() const;
  GainFlags flags() const;
  // Current applied gain in dB (0.0 = unity; negative = attenuation).
  double applied_gain_db() const;
  // The last evaluated chain delta (wet − dry, current metric, BEFORE
  // the sign flip that feeds the target) — a UI readout (FR §10.6).
  double delta_db() const;
  // True once BOTH meters hold a full window (freshness floor for the
  // delta; until then the target holds).
  bool delta_valid() const;

  // Code constants (FR-2's "not a UI setting" spirit, A2 form: window /
  // slot / streak lengths are code constants; MaxComp + ramp speed are
  // the UI-exposed parameters arriving with stage 2).
  static constexpr double kMeterWindowSec = 3.0;    // short-term window
  static constexpr double kDefaultRampTcSec = 0.3;  // 300 ms ramp (FR-5)
  // A2 §10.4: MaxComp default lowered 24 → 16 dB (range 0–48 unchanged).
  static constexpr double kDefaultMaxCompDb = 16.0;
  // Wall-slot silence threshold (newest 100 ms mean square ≤ this ⇒
  // digitally silent input, ≈ −120 dB FS). Same definition as the M1
  // meter-era gate: drives the hold AND both flags' streaks.
  static constexpr double kSilenceMeanSq = 1e-12;
  // 5.0 s of 100 ms slots (TS §9 ③) before a silence flag lights.
  static constexpr std::size_t kNoSignalSlots = 50;

 private:
  LoudnessMeter wet_meter_;  // main input (chain-processed)
  LoudnessMeter dry_meter_;  // aux tap (pre-chain); same engine ⇒ equal
                             // inputs give a usefully EXACT zero delta

  GainMetric metric_ = GainMetric::lufs;
  bool engaged_ = false;

  double sample_rate_ = 0.0;
  double max_comp_db_ = kDefaultMaxCompDb;
  double ramp_tc_ = kDefaultRampTcSec;

  GainFlags flags_;

  double applied_db_ = 0.0;  // the ramped applied gain
  double target_db_ = 0.0;   // what the ramp currently chases
  double last_delta_db_ = 0.0;  // last evaluated wet − dry (UI readout)

  // Evaluation-slot schedule (internal fixed 100 ms schedule; the R-D8
  // invariance needs AN internal schedule, and one only).
  double alpha_ = 0.0;
  std::uint64_t eval_slot_samples_ = 0;
  std::uint64_t slot_fed_ = 0;

  // Wall-side silence detection per tap (hold gate + flag streaks).
  double wet_slot_sq_ = 0.0;
  double dry_slot_sq_ = 0.0;
  std::size_t wet_silent_streak_ = 0;
  std::size_t dry_silent_streak_ = 0;

  // Slot-boundary work: delta evaluation + flag bookkeeping.
  void evaluate();
  // Per-sample one-pole advance of applied toward target; optional
  // observational emission of each post-step value (PB2a's contract,
  // carried into the A2 engine).
  void advance_ramp(std::size_t count, float* out = nullptr);
  // Raw sample energy of a chunk (wall-side silence detector).
  static double slot_energy(const float* samples, std::size_t count);
};

}  // namespace makegain::dsp
