// src/dsp/gain_computer.cpp — RED-phase stub (AGENTS.md rule 8).
//
// There must be NO real implementation here while the owner is reviewing
// the gain-computer specification in tests/dsp/gain_computer_test.cpp.
// What exists is the honest minimum that compiles against the header:
// every meaningful behavior is a constant/stub that CANNOT satisfy the
// assertions (no meter runs, no state machine, no reference, no codec) —
// so the red gate fails with real evidence, not with a fake pass.
//
// The GREEN commit replaces this file's contents wholesale.

#include "gain_computer.h"

namespace makegain::dsp {

GainComputer::GainComputer(double sample_rate)
    : meter_(sample_rate, kTrainingWindowSec), sample_rate_(sample_rate) {
  // Stub: the meter is never fed or read by process(); it exists only so
  // this translation unit compiles against the planned member layout.
}

void GainComputer::set_mode(GainMode mode) {
  mode_ = mode;  // stub: stores the value, runs no training state machine
}

void GainComputer::set_metric(GainMetric metric) {
  metric_ = metric;  // stub: no re-target can exist without a reference
}

void GainComputer::set_max_comp_db(double db) { max_comp_db_ = db; }

void GainComputer::set_ramp_tc(double seconds) { ramp_tc_ = seconds; }

void GainComputer::process(const float* /*samples*/, std::size_t /*count*/) {
  // Stub: intentionally does nothing — no meter feed, no ramp, no hold.
}

GainMode GainComputer::mode() const { return mode_; }

GainMetric GainComputer::metric() const { return metric_; }

ReferenceRecord GainComputer::reference() const {
  // Stub: no training run can complete, so the record stays invalid.
  return reference_.valid ? ReferenceRecord{} : reference_;
}

GainFlags GainComputer::flags() const { return flags_; }

double GainComputer::applied_gain_db() const {
  // Stub: permanently unity — no compensation can ever have been applied.
  return 0.0;
}

std::vector<std::uint8_t> GainComputer::encode_reference() const {
  // Stub: an invalid reference encodes to empty by design, and no valid
  // one can exist — the encoder is therefore unreachable-but-typed here.
  return {};
}

bool GainComputer::decode_reference(const std::uint8_t* /*data*/,
                                    std::size_t /*size*/) {
  // Stub: always refuses — decode must be all-or-nothing, and there is
  // nothing meaningful to accept.
  return false;
}

}  // namespace makegain::dsp
