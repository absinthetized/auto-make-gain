// src/dsp/gain_computer.cpp — A2 delta engine (PB2e RED: stub)
//
// The engine is honestly unable to do its job yet: process() advances
// nothing and writes nothing, so emitted arrays keep their trap values
// and every behavioral assertion fails with real evidence. The GREEN
// commit implements the slot-evaluated delta, the one-pole ramp, the
// hold gate and the flag machinery on top of the two meters wired here.

#include "gain_computer.h"

#include <cmath>

namespace makegain::dsp {

// Evaluation slot in seconds — the internal fixed schedule the R-D8
// chunk-invariance property needs (file-local on purpose: ONE schedule
// in the codebase mirrors the meter's 100 ms sub-block convention).
namespace {
constexpr double kEvalSlotSec = 0.1;
}  // namespace

GainComputer::GainComputer(double sample_rate)
    : wet_meter_(sample_rate, kMeterWindowSec),
      dry_meter_(sample_rate, kMeterWindowSec),
      sample_rate_(sample_rate) {
  // One-pole α per sample: applied += (target − applied)·α.
  const double per_sample = 1.0 / sample_rate_;
  alpha_ = 1.0 - std::exp(-per_sample / ramp_tc_);
  eval_slot_samples_ =
      static_cast<std::uint64_t>(sample_rate_ * kEvalSlotSec + 0.5);
}

void GainComputer::set_metric(GainMetric metric) { metric_ = metric; }

void GainComputer::set_max_comp_db(double db) { max_comp_db_ = db; }

void GainComputer::set_ramp_tc(double seconds) { ramp_tc_ = seconds; }

void GainComputer::set_engaged(bool engaged) { engaged_ = engaged; }

void GainComputer::process(const float* /*wet*/, const float* /*dry*/,
                           std::size_t /*count*/, float* /*out_gain_db*/) {
  // PB2e RED stub: cannot emit, cannot chase, cannot hold. The tests
  // against the retired FSM semantics (R1–R8, R-t1..t3) are gone with
  // that spec; the NEW R-D assertions fail below for the honest reason
  // that the engine's body does not exist yet.
}

GainMetric GainComputer::metric() const { return metric_; }

GainFlags GainComputer::flags() const { return flags_; }

double GainComputer::applied_gain_db() const { return applied_db_; }

double GainComputer::delta_db() const { return last_delta_db_; }

bool GainComputer::delta_valid() const {
  // Truthful already: full windows on BOTH taps = the freshness floor
  // for delta evaluation, regardless of whether we evaluate yet.
  return wet_meter_.snapshot().valid && dry_meter_.snapshot().valid;
}

}  // namespace makegain::dsp
