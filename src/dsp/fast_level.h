// src/dsp/fast_level.h — Fast-mode level follower (A5 FR-28).
//
// What this unit is: a SENSOR. Feed it samples, read a broadband level.
// It knows nothing about modes, taps, high-pass filters, targets or
// gain — those belong to GainComputer (m3-plan.md, "Engine design").
// Keeping it that dumb is what makes it reviewable on its own and
// replaceable (the deferred least-squares estimator would sit beside it,
// not inside it).
//
// Shape: a one-pole (exponential) mean square, per sample, tau = 15 ms.
// Why a one-pole and not a rectangular 15 ms block: at the Fast
// detector's band floor (80 Hz, FR-29) a 15 ms block spans barely more
// than one cycle, so the block's mean square ripples with the waveform.
// A one-pole averages the whole past and does not.
//
// Realtime contract: process() is called on the audio thread — no
// allocation, no locks, no I/O, no wall-clock reads. Construction is
// non-audio time.
//
// JUCE-free by design (TS §7): pure C++20, no framework types, so the
// test target can compile it without JUCE.

#pragma once

#include <cstddef>

namespace makegain::dsp {

class FastLevel {
 public:
  // Time constant of the exponential mean square, in seconds (A5 FR-28).
  // A test pins this number independently, so changing it here must be a
  // deliberate contract change, not a tune.
  static constexpr double kTimeConstantSec = 0.015;
  // Level floor for silence: -120 dBFS, so level_db() is always finite
  // (a log of zero would be -inf and poison every comparison downstream).
  static constexpr double kFloorMeanSq = 1e-12;

  // `sample_rate` is fixed for the engine's life (the owning engine is
  // re-created on rate changes, like the meters it owns).
  explicit FastLevel(double sample_rate);

  // Feed mono audio, any block size; does not modify the input.
  void process(const float* samples, std::size_t count);

  // Current mean square of the signal (linear; 1.0 == digital full-scale
  // square wave, 0.5 == full-scale sine).
  double mean_square() const;

  // 10*log10(mean_square), floored at -120 dB. Finite for silence.
  double level_db() const;

 private:
  double sample_rate_ = 0.0;
  // Per-sample one-pole coefficient: 1 - exp(-1 / (rate * tau)).
  double alpha_ = 0.0;
  double mean_square_ = 0.0;
};

}  // namespace makegain::dsp
