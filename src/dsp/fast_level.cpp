// src/dsp/fast_level.cpp — Fast-mode level follower (A5 FR-28), GREEN.
//
// The whole unit is one recursion: mean_square += (x*x - mean_square)*alpha,
// where alpha is the per-sample coefficient of a 15 ms one-pole. The
// derivation, because the number matters to the tests: after exactly N =
// rate*tau samples of a 0 -> 1 step the residual is (1-alpha)^N =
// exp(-1), i.e. the follower is 1 - 1/e = 63.2 % of the way, which is
// what "one time constant" means.

#include "fast_level.h"

#include <algorithm>
#include <cmath>

namespace makegain::dsp {

FastLevel::FastLevel(double sample_rate) : sample_rate_(sample_rate) {
  // Guard the degenerate rate rather than dividing by zero: an
  // instantaneous follower (alpha 1) is the only sane reading of a
  // missing sample rate, and it fails loudly in tests rather than
  // silently producing NaNs on the audio thread.
  if (sample_rate_ > 0.0) {
    const double per_sample = 1.0 / sample_rate_;
    alpha_ = 1.0 - std::exp(-per_sample / kTimeConstantSec);
  } else {
    alpha_ = 1.0;
  }
}

void FastLevel::process(const float* samples, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    const double x = static_cast<double>(samples[i]);
    mean_square_ += (x * x - mean_square_) * alpha_;
  }
}

double FastLevel::mean_square() const { return mean_square_; }

double FastLevel::level_db() const {
  // Floor first, log second: silence must read a number, never -inf.
  return 10.0 * std::log10(std::max(mean_square_, kFloorMeanSq));
}

}  // namespace makegain::dsp
