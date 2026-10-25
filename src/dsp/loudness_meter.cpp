// src/dsp/loudness_meter.cpp — the dual-metric rolling-window kernel (GREEN).
//
// Behavior contract lives in the header + tests/dsp/loudness_meter_test.cpp
// (B1 golden LUFS / B2 golden dB RMS / B3 sliding window / B4 anchor
// cross-tie). Notes that matter for maintenance:
//
//  - LUFS uses the BS.1770 loudness formula with the -0.691 constant,
//    which cancels the K-weighting chain's +0.691 dB gain at ~1 kHz —
//    at the anchor tone both metrics agree by calibration (B4).
//  - dB RMS is 10*log10(mean square) of the UNWEIGHTED signal over the
//    same window (FR-6a: same window, never cross-metric math).
//  - `valid` flips once `feed >= window`; before that, values are the
//    partial-window aggregates (documented, not garbage).

#include "loudness_meter.h"

#include <algorithm>
#include <cmath>

namespace makegain::dsp {

LoudnessMeter::LoudnessMeter(double sample_rate, double window_seconds)
    : kweight_(sample_rate) {
  sample_rate_ = sample_rate;
  sub_samples_ = static_cast<uint64_t>(
      std::llround(sample_rate * 0.1)); // 100 ms, the BS.1770 sub-block
  window_samples_ = static_cast<uint64_t>(
      std::llround(window_seconds * sample_rate));
  // Capacity reserved for the LARGEST span (3.0 s) + 1 open slot, so
  // set_window never allocates at runtime (A3 FR-23).
  const double cap_window = std::max(window_seconds, kMaxWindowSec);
  const uint64_t cap_samples = static_cast<uint64_t>(
      std::llround(cap_window * sample_rate));
  const auto nodes =
      static_cast<std::size_t>((cap_samples + sub_samples_ - 1) /
                               sub_samples_) + 1; // +1: the open node
  ring_.resize(nodes);
  scratch_.resize(static_cast<std::size_t>(sub_samples_));
}

void LoudnessMeter::set_window(double seconds) {
  const double clamped = std::min(std::max(seconds, 0.1), kMaxWindowSec);
  window_samples_ = static_cast<uint64_t>(
      std::llround(clamped * sample_rate_));
}

void LoudnessMeter::process(const float* samples, std::size_t count) {
  std::size_t i = 0;
  while (i < count) {
    Node& node = ring_[ring_next_];
    const std::size_t room =
        static_cast<std::size_t>(sub_samples_ - node.count);
    const std::size_t n = (count - i) < room ? (count - i) : room;

    // K-weight this sub-block chunk in a scratch copy — the caller's
    // buffer must stay untouched (metering is strictly non-destructive).
    for (std::size_t j = 0; j < n; ++j) {
      scratch_[j] = samples[i + j];
    }
    kweight_.process(scratch_.data(), n);

    for (std::size_t j = 0; j < n; ++j) {
      const double w = static_cast<double>(scratch_[j]);
      const double r = static_cast<double>(samples[i + j]);
      node.weighted_sq += w * w;
      node.raw_sq += r * r;
    }
    node.count += n;
    i += n;

    if (node.count == sub_samples_) { // slot complete: advance the ring
      ring_next_ = (ring_next_ + 1) % ring_.size();
      ring_[ring_next_] = Node{}; // fresh open slot (exact zeros again)
    }
    fed_ += n;
  }
}

LoudnessSnapshot LoudnessMeter::snapshot() const {
  // Walk the ring newest→oldest accumulating whole nodes; the last
  // (oldest) needed node contributes its tail proportionally — its
  // samples are all real within that node, the scaling only trims the
  // part of the node that aged past the window edge.
  double sum_w = 0.0, sum_r = 0.0;
  uint64_t taken = 0;
  std::size_t idx = ring_next_;
  if (ring_[idx].count == 0) {
    // The stream can land EXACTLY on a slot boundary (any host feeding
    // exact 100 ms multiples does; the gain machine's slot-aligned
    // captures always do). The currently-open node is then legitimately
    // empty while fully-accumulated history sits behind it — that is
    // NOT "ring not yet full". Step one slot older and walk from
    // there; a still-empty node later remains a genuine not-full state
    // (nothing has been fed into that part of the ring yet).
    idx = (idx + ring_.size() - 1) % ring_.size();
  }
  for (std::size_t visited = 0;
       visited < ring_.size() && taken < window_samples_;
       ++visited) {
    const Node& node = ring_[idx];
    if (node.count == 0) {
      break; // ring not yet full: behind it there is nothing fed
    }
    const uint64_t need = window_samples_ - taken;
    if (node.count <= need) {
      sum_w += node.weighted_sq;
      sum_r += node.raw_sq;
      taken += node.count;
    } else { // trim: only the newest `need` samples of this node fit
      const double frac = static_cast<double>(need) /
                          static_cast<double>(node.count);
      sum_w += node.weighted_sq * frac;
      sum_r += node.raw_sq * frac;
      taken = window_samples_;
    }
    idx = (idx + ring_.size() - 1) % ring_.size(); // step to older slot
  }

  if (fed_ < window_samples_ || taken < window_samples_) {
    LoudnessSnapshot partial;
    partial.valid = false; // not a full window yet: values may be partial
    return partial;
  }

  LoudnessSnapshot s;
  const double mean_w = sum_w / static_cast<double>(window_samples_);
  const double mean_r = sum_r / static_cast<double>(window_samples_);
  s.lufs = -0.691 + 10.0 * std::log10(mean_w);
  s.db_rms = 10.0 * std::log10(mean_r);
  s.valid = true;
  return s;
}

} // namespace makegain::dsp
