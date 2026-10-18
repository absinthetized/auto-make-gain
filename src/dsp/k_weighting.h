// src/dsp/k_weighting.h — K-weighting filter (BS.1770), STUB for red/green.
//
// MakeGain DSP layer: JUCE-free, pure C++17, no platform deps (TS §7).
// The K-weighting filter is the perception stage of ITU-R BS.1770 loudness
// measurement: a high-shelf (+4 dB above ~1.7 kHz) followed by a high-pass
// (~38 Hz), approximating an inverted 70-phon equal-loudness curve.
//
// ⚠ RED PHASE (AGENTS.md rule 8): this stub deliberately does nothing —
// it returns the input unchanged (identity "filter"). The test
// tests/dsp/k_weighting_test.cpp asserts real BS.1770 behavior and is
// expected to FAIL against this stub. The GREEN commit replaces this
// body with the actual 2-stage cascade.

#pragma once

#include <cstddef>

namespace makegain::dsp {

// One K-weighting instance per audio channel. Sample rate is fixed at
// construction; a rate change means a new instance (the plugin recreates
// it when the host reports a new rate).
class KWeighting {
public:
  explicit KWeighting(double sample_rate);

  // Process a mono block in place. Real-time safe: no allocation, no locks.
  void process(float* samples, std::size_t count);

private:
  double sample_rate_;
};

} // namespace makegain::dsp
