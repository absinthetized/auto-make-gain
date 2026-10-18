// src/dsp/k_weighting.cpp — STUB body for the red phase.
// See the header: identity behavior only; the BS.1770 cascade lands in
// the green commit. Kept as short as possible so the stub cannot
// "accidentally" pass tests by partial implementation.

#include "k_weighting.h"

namespace makegain::dsp {

KWeighting::KWeighting(double sample_rate) : sample_rate_(sample_rate) {}

void KWeighting::process(float* samples, std::size_t count) {
  // STUB: identity — loudness measures see an untouched signal.
  (void)samples;
  (void)count;
  (void)sample_rate_;
}

} // namespace makegain::dsp
