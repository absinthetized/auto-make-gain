// src/dsp/k_weighting.cpp — BS.1770 K-weighting, 2-stage cascade (GREEN).
//
// FORMULATION (the load-bearing decision, documenting an error we made
// once): ITU-R BS.1770-4 publishes a table of biquad coefficients valid
// at exactly 48 kHz. Two ways to generalize to arbitrary sample rates:
//
//   (a) RBJ Audio-EQ-Cookbook formulas fed with the standard's stage
//       parameters (f_r ≈ 1681.97 Hz, Q ≈ 0.707). Rejected: wrong
//       transition-band shape — RBJ defines the shelf corner
//       differently, so this Measured +0.44 dB at 1 kHz instead of
//       the standard's +0.691 dB (the same deviation pyloudnorm once
//       had, compensated with fc = 1500 Hz — see their issue #3).
//
//   (b) DeMan's K-based derivation (Brecht DeMan; the formulation
//       widely used by compliant meter implementations, e.g.
//       BS.1770-4-compliant pyloudnorm "DeMan" filter class): designed
//       to reproduce the standard's 48 kHz figure exactly at 48 kHz and
//       generalize via bilinear substitution K = tan(π fc / fs).
//       Chosen: this is the compliant per-rate path.
//
// Stage parameters (BS.1770-4, as used by the DeMan formulation):
//   stage 1  high-shelf: fc = 1681.9744509555319 Hz, G = 3.99984385397 dB,
//            Q = 0.7071752369554193
//   stage 2  high-pass:  fc =   38.13547087613982 Hz, Q = 0.5003270373253953
//
// Real-time safety: no allocation, no locks, fixed cost per sample.

#include "k_weighting.h"

#include <cmath>

namespace makegain::dsp {

// Stage 1 (DeMan): shelf coefficients for the block processing context.
// K = tan(pi*fc/fs) is the bilinear prewarp; Vb ≈ Vh^0.5 is the matched
// mid-band gain chosen (0.499666774155 exponent) so the cascade hits the
// standard's 48 kHz response exactly at 48 kHz.
KWeighting::Biquad
KWeighting::make_high_shelf(double sample_rate) {
  constexpr double fc = 1681.9744509555319;
  constexpr double gain_db = 3.99984385397;
  constexpr double q = 0.7071752369554193;

  const double k = std::tan(M_PI * fc / sample_rate);
  const double vh = std::pow(10.0, gain_db / 20.0);
  const double vb = std::pow(vh, 0.49966677415484777);
  const double kk = k * k;
  const double k_over_q = k / q;

  Biquad s;
  const double a0 = 1.0 + k_over_q + kk;
  s.b0 = (vh + vb * k_over_q + kk) / a0;
  s.b1 = 2.0 * (kk - vh) / a0;
  s.b2 = (vh - vb * k_over_q + kk) / a0;
  s.a1 = 2.0 * (kk - 1.0) / a0;
  s.a2 = (1.0 - k_over_q + kk) / a0;
  return s;
}

// Stage 2 (DeMan): 2nd-order HP with the standard's Q. b = {1, -2, 1}.
KWeighting::Biquad
KWeighting::make_high_pass(double sample_rate) {
  constexpr double fc = 38.13547087613982;
  constexpr double q = 0.5003270373253953;

  const double k = std::tan(M_PI * fc / sample_rate);
  const double kk = k * k;
  const double k_over_q = k / q;

  Biquad s;
  const double a0 = 1.0 + k_over_q + kk;
  s.b0 = 1.0;
  s.b1 = -2.0;
  s.b2 = 1.0;
  s.a1 = 2.0 * (kk - 1.0) / a0;
  s.a2 = (1.0 - k_over_q + kk) / a0;
  return s;
}

KWeighting::KWeighting(double sample_rate) {
  shelf_ = make_high_shelf(sample_rate);
  high_pass_ = make_high_pass(sample_rate);
}

void KWeighting::process(float* samples, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    const double x = static_cast<double>(samples[i]);
    const double y = high_pass_.tick(shelf_.tick(x));
    samples[i] = static_cast<float>(y);
  }
}

} // namespace makegain::dsp
