// src/dsp/k_weighting.h — K-weighting filter (BS.1770).
//
// MakeGain DSP layer: JUCE-free, pure C++17, no platform deps (TS §7).
// The K-weighting filter is the perception stage of ITU-R BS.1770
// loudness measurement: a high-shelf (+4 dB above ~1.7 kHz) followed by
// a high-pass (~38 Hz), approximating an inverted 70-phon
// equal-loudness curve. Two cascaded biquads, Transposed Direct Form II,
// coefficients computed at construction for the instance's sample rate.

#pragma once

#include <cstddef>

namespace makegain::dsp {

// One instance = one audio channel. A sample-rate change means a new
// instance (the plugin recreates it when the host reports a new rate).
class KWeighting {
public:
  explicit KWeighting(double sample_rate);

  // Process a mono block in place. Real-time safe: no allocation, no
  // locks, fixed cost per sample. Audio thread only (call from
  // processBlock); construction must have happened on non-audio time.
  void process(float* samples, std::size_t count);

private:
  // Transposed Direct Form II biquad. Stored type is double (the audio
  // path converts at the block boundary); z1/z2 are the state delays.
  struct Biquad {
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
    double tick(double x) {
      const double y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };

  // Biquad construction lives in the .cpp; keeping the coefficient
  // formulas out of the header is deliberate (they are the DSP knowledge
  // in this module and want the full derivation comments).
  // Cascade order matches the standard's listing; order is
  // mathematically irrelevant for these LTI stages.
  Biquad shelf_;
  Biquad high_pass_;

  static Biquad make_high_shelf(double sample_rate);
  static Biquad make_high_pass(double sample_rate);
};

} // namespace makegain::dsp
