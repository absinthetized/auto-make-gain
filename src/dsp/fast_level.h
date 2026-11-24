// src/dsp/fast_level.h — Fast-mode level follower (A5 FR-28), RED stub.
//
// RED phase (AGENTS.md rule 8): this header compiles and is wired into
// the test target, but every member returns a placeholder. The tests in
// tests/dsp/fast_level_test.cpp must fail until the GREEN commit brings
// the real exponential mean-square recursion in fast_level.cpp.
//
// What the finished unit is: a SENSOR. Feed it samples, read a broadband
// level. It knows nothing about modes, taps, high-pass filters, targets
// or gain — those belong to GainComputer (m3-plan.md, "Engine design").

#pragma once

#include <cstddef>

namespace makegain::dsp {

class FastLevel {
 public:
  // Time constant of the exponential mean square, in seconds (A5 FR-28).
  static constexpr double kTimeConstantSec = 0.015;
  // Level floor for silence: -120 dBFS, so level_db() is always finite.
  static constexpr double kFloorMeanSq = 1e-12;

  explicit FastLevel(double sample_rate) : sample_rate_(sample_rate) {}

  // Feed mono audio, any block size; does not modify the input.
  void process(const float* samples, std::size_t count) {
    (void)samples;
    (void)count;
  }

  // Current mean square of the signal (linear, 0..1 for full scale).
  double mean_square() const { return 0.0; }

  // 10*log10(mean_square), floored at -120 dB. Finite for silence.
  double level_db() const { return 0.0; }

 private:
  double sample_rate_ = 0.0;
};

}  // namespace makegain::dsp
