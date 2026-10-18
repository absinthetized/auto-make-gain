// tests/dsp/k_weighting_test.cpp — K-weighting behavioral tests (RED phase).
//
// What is asserted (the specification, reviewed by the owner before any
// implementation exists — AGENTS.md rule 8):
//
//   A1. Anchor at 1 kHz: |H| = +0.691 dB ± 0.1. (Spec-level correction
//       after review of the first red: the K-weighting chain has ~+0.691
//       dB gain at 997 Hz BY DESIGN — the −0.691 constant in the BS.1770
//       loudness formula, L = −0.691 + 10·log10(mean square), exists to
//       cancel it, so a 997 Hz reference tone reads its dBFS level.
//       The filter does NOT have unity gain at 1 kHz.)
//   A2. High shelf: |H| ≈ +4 dB at both 5 kHz and 10 kHz (±0.6 dB).
//       Published shelf gain G = 3.99984 dB above the corner (~1.7 kHz).
//   A3. Low-frequency rejection (relational, not absolute): gain(20 Hz)
//       at least 10 dB below gain(1 kHz). The standard's HP stage
//       (fc ≈ 38 Hz, Q ≈ 0.5) yields roughly −12…−14 dB relative to
//       1 kHz at 20 Hz (just under one octave below cutoff at the
//       12 dB/oct asymptote) — 10 dB is the safe hard floor; the exact
//       response is verified in the green commit against the cascade
//       built from the standard's own coefficient parameters.
//   A4. Sample-rate adaptation: A1/A2 hold at 44.1, 48 and 96 kHz within
//       the same tolerances (coefficients are computed per rate).
//
// Method: a long sine at each frequency, run through the filter, then
// measure steady-state RMS at the output vs. input. RMS-ratio avoids
// peak/phase traps and is what the loudness kernel itself consumes.

#include "k_weighting.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using makegain::dsp::KWeighting;

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

// Feed `freq_hz` through the filter, return output RMS / input RMS (linear).
double measured_gain_linear(double sample_rate, double freq_hz) {
  const int block = static_cast<int>(sample_rate) / 4; // 250 ms of signal
  const int settle = sample_rate;                      // 1 s warm-up (steady state)
  const int total = block + settle;
  std::vector<float> input(total);
  for (int i = 0; i < total; ++i) {
    input[i] = static_cast<float>(std::sin(2.0 * kPi * freq_hz * i / sample_rate));
  }
  std::vector<float> out = input; // input preserved: output measured separately
  KWeighting k(sample_rate);
  k.process(out.data(), static_cast<std::size_t>(total));

  double sum_in = 0.0, sum_out = 0.0;
  for (int i = settle; i < total; ++i) {
    sum_in += static_cast<double>(input[i]) * input[i];
    sum_out += static_cast<double>(out[i]) * out[i];
  }
  return std::sqrt(sum_out / sum_in);
}

// Amplitude (voltage) ratio → dB: factor 20 (10 is for power ratios).
// measured_gain_linear returns an RMS amplitude ratio, so 20·log10.
double to_db(double linear) { return 20.0 * std::log10(linear); }

} // namespace

// NOTE (red phase): the harness above is final — the green commit may
// only touch src/dsp/k_weighting.cpp. Against the identity stub the
// ratios measure ~0 dB at every frequency, so A2 fails (+4 dB expected),
// A3 fails (no rejection), A4's 10 kHz rows fail; A1 happens to pass
// (identity IS unity at 1 kHz) — an accepted, expected red, since unity
// at 1 kHz is the one property an identity filter legitimately has.

TEST_CASE("K-weighting: anchor +0.691 dB at 1 kHz (A1)", "[kweighting]") {
  const double g = measured_gain_linear(48000.0, 1000.0);
  // +0.691 dB: the chain gain the −0.691 loudness constant cancels.
  REQUIRE(to_db(g) == Catch::Approx(0.691).margin(0.1));
}

TEST_CASE("K-weighting: high shelf +4 dB (A2)", "[kweighting]") {
  for (const double freq : {5000.0, 10000.0}) {
    const double g = measured_gain_linear(48000.0, freq);
    REQUIRE(to_db(g) == Catch::Approx(4.0).margin(0.6));
  }
}

TEST_CASE("K-weighting: low-frequency rejection (A3)", "[kweighting]") {
  const double g_1k = measured_gain_linear(48000.0, 1000.0);
  const double g_20 = measured_gain_linear(48000.0, 20.0);
  // 10 dB floor: see the A3 note above (true value ≈ −12…−14 dB).
  REQUIRE(to_db(g_20) < to_db(g_1k) - 10.0);
}

// A3b (owner, 2026-10-18): shape, not just depth — within the rejection
// band the magnitude must rise monotonically toward 1 kHz. A single-point
// depth check (A3) can be defeated by a wrong-Q resonance or a sign
// error that still lands below threshold at 20 Hz but bumps *above* its
// neighbors near the corner. A monotone run of probe points kills that
// failure mode. Epsilon 0.05 dB absorbs numerical wobble only.
TEST_CASE("K-weighting: monotonic rise through the rejection band (A3b)",
          "[kweighting]") {
  constexpr double kEps = 0.05; // dB — numerical wobble tolerance
  const std::vector<double> probes = {20.0, 30.0, 50.0, 100.0, 300.0, 1000.0};
  double prev = -1e9;
  for (const double f : probes) {
    const double db = to_db(measured_gain_linear(48000.0, f));
    INFO("frequency " << f << " Hz -> " << db << " dB");
    REQUIRE(db > prev + kEps); // strictly rising, not merely non-decreasing
    prev = db;
  }
}

TEST_CASE("K-weighting: sample-rate adaptation (A4)", "[kweighting]") {
  for (const double sr : {44100.0, 48000.0, 96000.0}) {
    const double g_1k = measured_gain_linear(sr, 1000.0);
    const double g_10k = measured_gain_linear(sr, 10000.0);
    REQUIRE(to_db(g_1k) == Catch::Approx(0.691).margin(0.1));
    REQUIRE(to_db(g_10k) == Catch::Approx(4.0).margin(0.6));
  }
}
