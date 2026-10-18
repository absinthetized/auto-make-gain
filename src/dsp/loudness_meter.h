// src/dsp/loudness_meter.h — dual-metric loudness kernel (BS.1770 short-term
// LUFS + dB RMS over one window), STUB for red/green.
//
// MakeGain DSP layer: JUCE-free, pure C++17 (TS §7).
//
// What this module is for in MakeGain: FR-6a — one training pass produces
// BOTH references (LUFS and dB RMS) over the SAME window, and Adjust
// measures the same pair live, so the metric switch never re-measures and
// never mixes math (FR-8).
//
// ⚠ RED PHASE (AGENTS.md rule 8): this stub returns fixed constants;
// every golden assertion in tests/dsp/loudness_meter_test.cpp is expected
// to FAIL against it. The green commit fills the real kernel:
// K-weighted power via k_weighting.h + per-100 ms sub-block accumulation
// over the window (BS.1770 short-term style).

#pragma once

#include <cstddef>
#include <cstdint>

namespace makegain::dsp {

// A point-in-time reading of both metrics over the rolling window.
// Values are dB-like (LUFS is dBFS-anchored; -inf means "no signal in
// the window" rather than an error).
struct LoudnessSnapshot {
  double lufs = 0.0;      // -0.691 + 10*log10(mean square, K-weighted)
  double db_rms = 0.0;    // 10*log10(mean square, unweighted), dBFS scale
  bool valid = false;     // true once a full window of audio has been seen
};

class LoudnessMeter {
public:
  // window_seconds: the measurement window (the training window length
  // flows in from the processor; FR-2: 3.0 s default, code constant).
  LoudnessMeter(double sample_rate, double window_seconds);

  // Feed mono audio, in place, any block size. Real-time safe: no
  // allocation, no locks (audio thread only).
  void process(float* samples, std::size_t count);

  // Read the current both-metric snapshot. Cheap (aggregated values,
  // not a recount).
  LoudnessSnapshot snapshot() const;

private:
  double sample_rate_;
};

} // namespace makegain::dsp
