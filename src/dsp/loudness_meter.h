// src/dsp/loudness_meter.h — dual-metric loudness kernel: BS.1770-style
// short-term LUFS + dB RMS over one rolling window.
//
// MakeGain DSP layer: JUCE-free, pure C++17 (TS §7).
//
// What this module is for in MakeGain: FR-6a — one training pass produces
// BOTH references (LUFS and dB RMS) over the SAME window, and Adjust
// measures the same pair live, so the metric switch never re-measures and
// never mixes math (FR-8). The window length comes from the processor
// (FR-2: 3.0 s default, code constant — not a UI setting).
//
// Accumulation design: 100 ms sub-blocks (the BS.1770 metering convention)
// accumulated as exact sum-of-squares in fixed slots, so the sliding
// window never re-multiplies old samples and floating-point drift cannot
// grow with session length (drift is bounded by one sub-block's arithmetic,
// not by hours of audio).

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "k_weighting.h"

namespace makegain::dsp {

// A point-in-time reading of both metrics over the rolling window.
// Values are dB-like; -inf means "no signal in the window", not an error.
struct LoudnessSnapshot {
  double lufs = 0.0;    // -0.691 + 10*log10(mean square, K-weighted)
  double db_rms = 0.0;  // 10*log10(mean square, unweighted), dBFS scale
  bool valid = false;   // true once a full window of audio has been seen
};

class LoudnessMeter {
 public:
  // `window_seconds` fixes the capacity: the ring ALWAYS reserves the
  // max window (A3: 3.0 s) plus the open slot, so `set_window` later
  // re-aims the span at runtime with zero allocation (FR-23's Short ↔
  // Fast switch happens through a parameter cross on the audio thread).
  LoudnessMeter(double sample_rate, double window_seconds);

  // Feed mono audio, any block size; does NOT modify the input. Real-time
  // safe: no allocation, no locks (audio thread only; construction on
  // non-audio time).
  void process(const float* samples, std::size_t count);

  // Read the current both-metric snapshot. Cheap: aggregated sub-block
  // sums, never a recount of the window.
  LoudnessSnapshot snapshot() const;

  // Runtime window re-aim (A3 FR-23): changes ONLY the aggregation span
  // (ring capacity is fixed at construction). Allocation-free, guarded
  // cheap enough to call per block; clamped to [0.1, 3.0] s. Validity
  // re-evaluates against the new span: shrinking restores validity from
  // the existing history, growing waits for the span to fill (honest).
  void set_window(double seconds);

 private:
  // One 100 ms accumulation slot. A complete node holds an exact
  // sum of squares over exactly `sub_samples_` samples.
  struct Node {
    double weighted_sq = 0.0; // K-weighted squares
    double raw_sq = 0.0;      // unweighted squares
    uint64_t count = 0;
  };

  // Largest span the capacity is reserved for (A3: Short = 3 s).
  static constexpr double kMaxWindowSec = 3.0;

  KWeighting kweight_;
  std::vector<Node> ring_;   // circular, oldest→newest implicit via index
  std::vector<float> scratch_; // sub-block-sized buffer for K-weighting
  std::size_t ring_next_ = 0;  // index of the currently-open node
  uint64_t window_samples_ = 0;
  uint64_t sub_samples_ = 0;
  uint64_t fed_ = 0;           // total samples ever fed (validity check)
  double sample_rate_ = 0.0;   // kept for set_window's span arithmetic
};

} // namespace makegain::dsp
