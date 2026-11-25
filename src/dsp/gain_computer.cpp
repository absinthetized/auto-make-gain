// src/dsp/gain_computer.cpp — the A2 delta engine (PB2e GREEN).
//
// Per-sample structure of process(): stream `count` samples in chunks
// bounded by the active mode's evaluation slot (Fast 5 ms / Smooth
// 100 ms), feeding BOTH detectors, ramping toward the current target,
// and at every slot boundary re-evaluating the chain delta and the
// wall-side silence verdicts. Assignment-host chunk sizes can never
// change the trajectory on either grid (chunk-invariance tests).
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

GainComputer::GainComputer(double sample_rate)
    : wet_meter_(sample_rate, kMeterWindowSec),
      dry_meter_(sample_rate, kMeterWindowSec),
      fast_wet_(sample_rate),
      fast_dry_(sample_rate),
      sample_rate_(sample_rate) {
  set_ramp_tc(kDefaultRampTcSec);
  rebuild_grid();
  // Scratch: one 100 ms chunk (the larger grid), allocated ONCE here
  // (construction is non-audio time; process() stays allocation-free).
  const auto scratch_n = static_cast<std::size_t>(
      sample_rate_ * kSmoothEvalSlotSec + 0.5);
  hp_scratch_wet_.resize(scratch_n);
  hp_scratch_dry_.resize(scratch_n);
  fast_scratch_wet_.resize(scratch_n);
  fast_scratch_dry_.resize(scratch_n);
  // Both HP chains start coherent: Smooth at the user corner (unused
  // while disabled), Fast at the 80 Hz floor (FR-29).
  const double fast_hz = meter_hp_enabled_
                             ? std::max(meter_hp_hz_, kFastHpFloorHz)
                             : kFastHpFloorHz;
  recompute_hp_coefficients(hp_smooth_wet_, hp_smooth_dry_, meter_hp_hz_);
  recompute_hp_coefficients(hp_fast_wet_, hp_fast_dry_, fast_hz);
}

void GainComputer::rebuild_grid() {
  const double slot = (mode_ == GainMode::fast) ? kFastEvalSlotSec
                                                : kSmoothEvalSlotSec;
  eval_slot_samples_ = std::max<std::uint64_t>(
      1, static_cast<std::uint64_t>(sample_rate_ * slot + 0.5));
  // A mid-slot mode change drops any partial slot: the two grids have
  // different lengths, so carrying energy across them is meaningless.
  slot_fed_ = 0;
  wet_slot_sq_ = 0.0;
  dry_slot_sq_ = 0.0;
}

void GainComputer::set_mode(GainMode mode) {
  // FR-30: a detector change under a hold would move the readouts, so
  // the switch is refused while frozen. Nothing else is disturbed — both
  // detectors stay warm.
  if (frozen_) return;
  if (mode_ == mode) return;
  mode_ = mode;
  rebuild_grid();
}

GainMode GainComputer::mode() const { return mode_; }

void GainComputer::set_metric(GainMetric metric) {
  // Interim M3b shim: the panel's Metric switch drives the mode.
  set_mode(metric == GainMetric::db_rms ? GainMode::fast
                                        : GainMode::smooth);
}

void GainComputer::set_meter_window(double /*seconds*/) {
  // Interim M3b shim: the Response window retires with FR-28 (the span
  // is mode-fixed), so the call does nothing.
}

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
    // Smooth path (FR-26 user HP): the filter lives ONLY in this path —
    // audio out is never touched. Disabled = the exact A2 feed (R-F8).
    const float* wet_m = wet + i;
    const float* dry_m = dry + i;
    if (meter_hp_enabled_) {
      hp_process(hp_smooth_wet_, wet_m, hp_scratch_wet_.data(), take);
      hp_process(hp_smooth_dry_, dry_m, hp_scratch_dry_.data(), take);
      wet_m = hp_scratch_wet_.data();
      dry_m = hp_scratch_dry_.data();
    }
    wet_meter_.process(wet_m, take);
    dry_meter_.process(dry_m, take);

    // Fast path (FR-29): ALWAYS filtered at the 80 Hz floor or above,
    // and fed in BOTH modes so a switch re-targets from warm state.
    hp_process(hp_fast_wet_, wet + i, fast_scratch_wet_.data(), take);
    hp_process(hp_fast_dry_, dry + i, fast_scratch_dry_.data(), take);
    fast_wet_.process(fast_scratch_wet_.data(), take);
    fast_dry_.process(fast_scratch_dry_.data(), take);

    advance_ramp(take, out_gain_db ? out_gain_db + i : nullptr);
    // Wall-side energy of the newest slot, per tap (digital-silence
    // verdict is per TAP — the hold gate is the stricter of the two).
    wet_slot_sq_ += slot_energy(wet + i, take);
    dry_slot_sq_ += slot_energy(dry + i, take);
    i += take;
    slot_fed_ += take;
    total_fed_ += take;
    if (slot_fed_ == eval_slot_samples_) {
      slot_fed_ = 0;
      const double slot_sec =
          static_cast<double>(eval_slot_samples_) / sample_rate_;
      const double wet_ms = wet_slot_sq_ /
                            static_cast<double>(eval_slot_samples_);
      const double dry_ms = dry_slot_sq_ /
                            static_cast<double>(eval_slot_samples_);
      wet_slot_sq_ = 0.0;
      dry_slot_sq_ = 0.0;
      // Streaks accumulate in SECONDS: 5 s means the same on either grid.
      wet_silent_sec_ = (wet_ms <= kSilenceMeanSq)
                            ? wet_silent_sec_ + slot_sec : 0.0;
      dry_silent_sec_ = (dry_ms <= kSilenceMeanSq)
                            ? dry_silent_sec_ + slot_sec : 0.0;
      flags_.no_signal_wet = (wet_silent_sec_ >= kNoSignalSec);
      flags_.dry_tap_missing = (dry_silent_sec_ >= kNoSignalSec);
      evaluate();
    }
  }
}

void GainComputer::evaluate() {
  // Active-mode readings. Both detectors run, but only the selected
  // pair drives the delta and the readouts (FR-28's unit rule: a level
  // in Fast, LUFS in Smooth).
  double wet_db = 0.0;
  double dry_db = 0.0;
  bool wet_ok = false;
  bool dry_ok = false;
  if (mode_ == GainMode::fast) {
    wet_db = fast_wet_.level_db();
    dry_db = fast_dry_.level_db();
    // Warm-up floor: the follower's level is only trusted after a few
    // time constants, so the target does not chase the startup swell.
    wet_ok = dry_ok = delta_valid();
  } else {
    const auto wet_snap = wet_meter_.snapshot();
    const auto dry_snap = dry_meter_.snapshot();
    wet_db = wet_snap.lufs;
    dry_db = dry_snap.lufs;
    wet_ok = wet_snap.valid;
    dry_ok = dry_snap.valid;
  }
  // Hold gate (FR §10.4): EITHER tap's newest slot below the silence
  // floor makes the delta meaningless (a silent reading is not a
  // loudness). silent_sec == 0 ⇔ the newest slot carried signal.
  const bool either_silent =
      (wet_silent_sec_ != 0.0) || (dry_silent_sec_ != 0.0);
  const bool delta_ok = wet_ok && dry_ok && !either_silent;

  // Per-tap readouts (FR §10.6): the ACTIVE mode's reading; a
  // wet-only-valid Smooth state still shows the wet reading.
  if (wet_ok) last_wet_db_ = wet_db;
  if (dry_ok) last_dry_db_ = dry_db;

  // The delta readout tracks valid, non-silent comparisons regardless of
  // engagement — the UI shows the chain delta even while bypassed (§10.6).
  if (delta_ok) last_delta_db_ = wet_db - dry_db;

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

GainFlags GainComputer::flags() const { return flags_; }

double GainComputer::applied_gain_db() const { return applied_db_; }

double GainComputer::delta_db() const { return last_delta_db_; }

bool GainComputer::delta_valid() const {
  // Freshness floor for the delta. Smooth: full windows on BOTH taps.
  // Fast: both followers past their warm-up. The wall silence gate adds
  // the hold on top of this.
  if (mode_ == GainMode::fast) {
    const auto warm = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(sample_rate_ * kFastWarmupSec + 0.5));
    return total_fed_ >= warm;
  }
  return wet_meter_.snapshot().valid && dry_meter_.snapshot().valid;
}

// ---------------------------------------------------------------------------
// A3 (PR-C) — the FR-23/24/26 surfaces, now real (green commit). All four
// are allocation-free and change-guarded so the processor may call them
// from every processBlock (audio thread) per the A2 crossing pattern.
// ---------------------------------------------------------------------------

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
  // Smooth: the user's corner, used only while enabled.
  recompute_hp_coefficients(hp_smooth_wet_, hp_smooth_dry_, meter_hp_hz_);
  hp_smooth_wet_.clear();
  hp_smooth_dry_.clear();
  // Fast: the 80 Hz floor applies whether or not the user enabled HP.
  const double fast_hz = meter_hp_enabled_
                             ? std::max(meter_hp_hz_, kFastHpFloorHz)
                             : kFastHpFloorHz;
  recompute_hp_coefficients(hp_fast_wet_, hp_fast_dry_, fast_hz);
  hp_fast_wet_.clear();
  hp_fast_dry_.clear();
}

void GainComputer::recompute_hp_coefficients(HpStage& wet, HpStage& dry,
                                             double hz)
{
  // 4th-order Butterworth high-pass = two cascaded 2nd-order sections
  // (Q = 0.541..., Q = 1.306...) sharing the corner. RBJ cookbook form
  // (sample-rate-derived, same KWeighting spirit as FR-D1's DeMan rows).
  const double w0 = 2.0 * std::numbers::pi * hz / sample_rate_;
  const double cos_w0 = std::cos(w0);
  const double sin_w0 = std::sin(w0);
  for (HpBiquad* q : {&wet.q1, &dry.q1}) {
    const double alpha = sin_w0 / (2.0 * hp_stage_Q1_);
    const double a0 = 1.0 + alpha;
    q->b0 = (1.0 + cos_w0) / 2.0 / a0;
    q->b1 = -(1.0 + cos_w0) / a0;
    q->b2 = (1.0 + cos_w0) / 2.0 / a0;
    q->a1 = -2.0 * cos_w0 / a0;
    q->a2 = (1.0 - alpha) / a0;
  }
  for (HpBiquad* q : {&wet.q2, &dry.q2}) {
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
