// src/dsp/loudness_meter.cpp — STUB body for the red phase.
// Identity-neutrality only: constants that cannot satisfy the golden
// assertions. The real accumulation kernel lands in the green commit.

#include "loudness_meter.h"

namespace makegain::dsp {

LoudnessMeter::LoudnessMeter(double sample_rate, double window_seconds)
    : sample_rate_(sample_rate) {
  (void)window_seconds;
}

void LoudnessMeter::process(float* samples, std::size_t count) {
  (void)samples;
  (void)count;
  (void)sample_rate_;
}

LoudnessSnapshot LoudnessMeter::snapshot() const {
  // STUB: constant values far from any real measurement.
  return LoudnessSnapshot{.lufs = -144.0, .db_rms = -144.0, .valid = false};
}

} // namespace makegain::dsp
