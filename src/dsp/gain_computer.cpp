// src/dsp/gain_computer.cpp — the A2 delta engine (PB2e GREEN).
//
// Per-sample structure of process(): stream `count` samples in ≤ one
// internal 100 ms evaluation-slot steps (assignment-host chunk sizes can
// never change the trajectory — R-D8), feeding BOTH meters, ramping both
// taps' samples toward the current target, and at every slot boundary
// re-evaluating the chain delta and the wall-side silence verdicts.
//
// Target policy (evaluate(), on the audio thread, slot boundaries only):
//   bypassed            → target 0 dB (ramp home — FR §10.3)
//   delta invalid       → hold: target := current applied gain (freeze
//                         the chase exactly where it is — FR §10.4)
//   delta valid         → target := clamp(−(wet − dry), ±MaxComp)
// The emitted per-sample value (PB2a's contract, carried here) is the
// applied gain AFTER that sample's ramp step; the host processor
// multiplies both channels by 10^(dB/20) (FR-11: no lookahead).

#include "gain_computer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace makegain::dsp {

// Evaluation slot in seconds — the internal fixed schedule the R-D8
// chunk-invariance property needs (file-local on purpose: ONE schedule
// in the codebase; mirrors the meter's 100 ms sub-block convention).
namespace {
constexpr double kEvalSlotSec = 0.1;
}  // namespace

GainComputer::GainComputer(double sample_rate)
    : wet_meter_(sample_rate, kMeterWindowSec),
      dry_meter_(sample_rate, kMeterWindowSec),
      sample_rate_(sample_rate) {
  set_ramp_tc(kDefaultRampTcSec);
  eval_slot_samples_ =
      static_cast<std::uint64_t>(sample_rate_ * kEvalSlotSec + 0.5);
  // Metering-HP scratch: one eval-slot-sized pair, allocated ONCE here
  // (construction is non-audio time; process() stays allocation-free).
  const auto scratch_n = static_cast<std::size_t>(eval_slot_samples_);
  hp_scratch_wet_.resize(scratch_n);
  hp_scratch_dry_.resize(scratch_n);
}

void GainComputer::set_metric(GainMetric metric) { metric_ = metric; }

void GainComputer::set_max_comp_db(double db) { max_comp_db_ = db; }

void GainComputer::set_ramp_tc(double seconds) {
  // One-pole α per sample: applied += (target − applied)·α. Cached
  // (the R-D6 step bound is expressed against this exact shape).
  if (!(seconds > 0.0)) seconds = kDefaultRampTcSec;
  ramp_tc_ = seconds;
  const double per_sample = 1.0 / sample_rate_;
  alpha_ = 1.0 - std::exp(-per_sample / ramp_tc_);
}

void GainComputer::set_engaged(bool engaged) { engaged_ = engaged; }

void GainComputer::process(const float* wet, const float* dry,
                           std::size_t count, float* out_gain_db) {
  std::size_t i = 0;
  while (i < count) {
    const std::uint64_t room = eval_slot_samples_ - slot_fed_;
    const std::size_t take = static_cast<std::size_t>(
        std::min<std::uint64_t>(room, count - i));
    // Meter feeds: the HP (FR-26) lives ONLY in this path — audio out
    // is untouched forever. Disabled = the exact A2 feed (R-F8).
    const float* wet_m = wet + i;
    const float* dry_m = dry + i;
    if (meter_hp_enabled_) {
      hp_process(hp_wet_stage_, wet_m, hp_scratch_wet_.data(), take);
      hp_process(hp_dry_stage_, dry_m, hp_scratch_dry_.data(), take);
      wet_m = hp_scratch_wet_.data();
      dry_m = hp_scratch_dry_.data();
    }
    wet_meter_.process(wet_m, take);
    dry_meter_.process(dry_m, take);
    advance_ramp(take, out_gain_db ? out_gain_db + i : nullptr);
    // Wall-side energy of the newest slot, per tap (digital-silence
    // verdict is per TAP — the hold gate is the stricter of the two).
    wet_slot_sq_ += slot_energy(wet + i, take);
    dry_slot_sq_ += slot_energy(dry + i, take);
    i += take;
    slot_fed_ += take;
    if (slot_fed_ == eval_slot_samples_) {
      slot_fed_ = 0;
      const double wet_ms = wet_slot_sq_ /
                            static_cast<double>(eval_slot_samples_);
      const double dry_ms = dry_slot_sq_ /
                            static_cast<double>(eval_slot_samples_);
      wet_slot_sq_ = 0.0;
      dry_slot_sq_ = 0.0;
      // Streaks: 1 slot silent = 100 ms of digital silence at the wall.
      wet_silent_streak_ =
          (wet_ms <= kSilenceMeanSq) ? wet_silent_streak_ + 1 : 0;
      dry_silent_streak_ =
          (dry_ms <= kSilenceMeanSq) ? dry_silent_streak_ + 1 : 0;
      flags_.no_signal_wet = (wet_silent_streak_ >= kNoSignalSlots);
      flags_.dry_tap_missing = (dry_silent_streak_ >= kNoSignalSlots);
      evaluate();
    }
  }
}

void GainComputer::evaluate() {
  const auto wet_snap = wet_meter_.snapshot();
  const auto dry_snap = dry_meter_.snapshot();
  const bool fresh = wet_snap.valid && dry_snap.valid;
  // Hold gate (FR §10.4): EITHER tap's newest slot below the silence
  // floor makes the delta meaningless (a silent meter reading is not a
  // loudness). streak == 0 ⇔ the newest slot carried signal.
  const bool either_silent =
      (wet_silent_streak_ != 0) || (dry_silent_streak_ != 0);
  const bool delta_ok = fresh && !either_silent;

  // Per-tap readouts (FR §10.6 / PR-C UI): valid snapshots only, in the
  // CURRENT metric (a wet-only-valid state still shows the wet reading).
  if (wet_snap.valid)
    last_wet_db_ = (metric_ == GainMetric::lufs) ? wet_snap.lufs
                                                 : wet_snap.db_rms;
  if (dry_snap.valid)
    last_dry_db_ = (metric_ == GainMetric::lufs) ? dry_snap.lufs
                                                 : dry_snap.db_rms;

  // The delta readout tracks valid, non-silent comparisons regardless of
  // engagement — the UI shows the chain delta even while bypassed (§10.6).
  if (delta_ok) {
    last_delta_db_ = (metric_ == GainMetric::lufs)
                         ? wet_snap.lufs - dry_snap.lufs
                         : wet_snap.db_rms - dry_snap.db_rms;
  }

  // Policy order matters (PB2e first-run lesson): the FREEZE decision
  // dominates everything — a frozen plugin is a static volume (FR-24),
  // even while bypassed (the Active toggle's ramp-home must not fight
  // the lock). Evaluation above still ran so readouts stay fresh, but
  // the ACTUATION is locked: the target is the held applied gain, and
  // since advance_ramp converges toward a constant target the emitted
  // value is bit-constant too (R-F1). Release re-enters the ordinary
  // evaluation seamlessly (R-F2).
  if (frozen_) {
    target_db_ = applied_db_;
    flags_.clamped = false;
  } else if (!engaged_) {
    target_db_ = 0.0;              // ramp home to unity (FR §10.3)
    flags_.clamped = false;
  } else if (!delta_ok) {
    // Hold: freeze the chase exactly where the applied gain is — the
    // per-sample ramp keeps converging to that (now-constant) target,
    // so nothing drifts and nothing resets (R-D7).
    target_db_ = applied_db_;
    flags_.clamped = false;
  } else {
    // FR-10 in A2 form: null the chain delta, within ±MaxComp.
    const double raw = -(last_delta_db_);
    flags_.clamped = (std::abs(raw) > max_comp_db_);
    target_db_ = std::clamp(raw, -max_comp_db_, max_comp_db_);
  }
}

void GainComputer::advance_ramp(std::size_t count, float* out) {
  for (std::size_t j = 0; j < count; ++j) {
    applied_db_ += (target_db_ - applied_db_) * alpha_;
    if (out != nullptr) {
      out[j] = static_cast<float>(applied_db_);  // observation, not compute
    }
  }
}

double GainComputer::slot_energy(const float* samples, std::size_t count) {
  // Sum of squares of the chunk (the caller divides by the slot length
  // to get the mean square — same convention as the M1-era gate).
  double sum_sq = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    const double x = static_cast<double>(samples[i]);
    sum_sq += x * x;
  }
  return sum_sq;
}

GainMetric GainComputer::metric() const { return metric_; }

GainFlags GainComputer::flags() const { return flags_; }

double GainComputer::applied_gain_db() const { return applied_db_; }

double GainComputer::delta_db() const { return last_delta_db_; }

bool GainComputer::delta_valid() const {
  // Freshness floor for the delta: full windows on BOTH taps. The wall
  // silence gate adds the hold on top of this.
  return wet_meter_.snapshot().valid && dry_meter_.snapshot().valid;
}

// ---------------------------------------------------------------------------
// A3 (PR-C) — the FR-23/24/26 surfaces, now real (green commit). All four
// are allocation-free and change-guarded so the processor may call them
// from every processBlock (audio thread) per the A2 crossing pattern.
// ---------------------------------------------------------------------------

void GainComputer::set_meter_window(double seconds)
{
  // Clamp to the capacity-backed span range; guard-compare so the per-
  // block call is two float writes at steady state.
  const double clamped = std::min(std::max(seconds, 0.1), kMeterWindowSec);
  if (clamped == meter_window_) return;
  meter_window_ = clamped;
  wet_meter_.set_window(clamped);
  dry_meter_.set_window(clamped);
}

void GainComputer::set_frozen(bool frozen)
{
  if (frozen_ == frozen) return;
  frozen_ = frozen;
  if (frozen_) {
    // Immediate lock: the current applied gain becomes the target right
    // now (not at the next slot boundary) — engaging Freeze mid-slot
    // does not wander (FR-24).
    target_db_ = applied_db_;
  }
}

bool GainComputer::frozen() const { return frozen_; }

void GainComputer::set_meter_hp(bool enabled, double hz)
{
  // Change-guard: identical calls (every processBlock) only touch
  // scalars. On a REAL change, coefficients recompute and filter states
  // reset (no stale state audible through the re-aimed meters).
  const double clamped_hz = std::min(std::max(hz, 20.0), 400.0);
  if (meter_hp_enabled_ == enabled && meter_hp_hz_ == clamped_hz) return;
  meter_hp_enabled_ = enabled;
  meter_hp_hz_ = clamped_hz;
  recompute_hp_coefficients(clamped_hz);
  hp_wet_stage_.clear();
  hp_dry_stage_.clear();
}

void GainComputer::recompute_hp_coefficients(double hz)
{
  // 4th-order Butterworth high-pass = two cascaded 2nd-order sections
  // (Q = 0.541..., Q = 1.306...) sharing the corner. RBJ cookbook form
  // (sample-rate-derived, same KWeighting spirit as FR-D1's DeMan rows).
  const double w0 = 2.0 * std::numbers::pi * hz / sample_rate_;
  const double cos_w0 = std::cos(w0);
  const double sin_w0 = std::sin(w0);
  for (HpBiquad* q : {&hp_wet_stage_.q1, &hp_dry_stage_.q1}) {
    const double alpha = sin_w0 / (2.0 * hp_stage_Q1_);
    const double a0 = 1.0 + alpha;
    q->b0 = (1.0 + cos_w0) / 2.0 / a0;
    q->b1 = -(1.0 + cos_w0) / a0;
    q->b2 = (1.0 + cos_w0) / 2.0 / a0;
    q->a1 = -2.0 * cos_w0 / a0;
    q->a2 = (1.0 - alpha) / a0;
  }
  for (HpBiquad* q : {&hp_wet_stage_.q2, &hp_dry_stage_.q2}) {
    const double alpha = sin_w0 / (2.0 * hp_stage_Q2_);
    const double a0 = 1.0 + alpha;
    q->b0 = (1.0 + cos_w0) / 2.0 / a0;
    q->b1 = -(1.0 + cos_w0) / a0;
    q->b2 = (1.0 + cos_w0) / 2.0 / a0;
    q->a1 = -2.0 * cos_w0 / a0;
    q->a2 = (1.0 - alpha) / a0;
  }
}

void GainComputer::hp_process(HpStage& stage, const float* in, float* out,
                           std::size_t count)
{
  for (std::size_t j = 0; j < count; ++j) {
    double y = stage.q1.step(static_cast<double>(in[j]));
    out[j] = static_cast<float>(stage.q2.step(y));
  }
}

double GainComputer::wet_loudness_db() const { return last_wet_db_; }
double GainComputer::dry_loudness_db() const { return last_dry_db_; }

}  // namespace makegain::dsp
