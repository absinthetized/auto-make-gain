// tests/dsp/fast_level_test.cpp — Fast-mode level follower (RED phase).
//
// What is asserted (the specification — owner reviews before any real
// implementation exists, AGENTS.md rule 8). FastLevel is a SENSOR, not a
// decision maker: samples in, a broadband level out. It knows nothing
// about modes, taps, HP filters or targets (those live in GainComputer,
// A5 / m3-plan.md).
//
//   G1. Steady state: constant amplitude 0.5 reads mean square 0.25 and
//       level -6.02 dB (10*log10(0.25)).
//   G2. One-pole timing: after exactly one time constant of a 0 -> 1.0
//       step, the mean square is 1 - 1/e = 0.6321 (0.1 % tolerance).
//       This pins tau = 15 ms as a real time constant, not a label.
//   G3. Release is the same shape: from steady 1.0, one time constant of
//       silence leaves 1/e = 0.3679 (no hidden asymmetric release).
//   G4. Chunk invariance: the same signal fed as one block or as ragged
//       chunks produces the same level (1e-12). The follower is a
//       per-sample recursion; blocking must not matter.
//   G5. Silence is finite and floored: zeros read mean square 0 and a
//       level at or below the -120 dB floor, never -inf or NaN.

#include "fast_level.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <iterator>
#include <numbers>
#include <vector>

using makegain::dsp::FastLevel;

namespace {

constexpr double kRate = 48000.0;
// The specification's time constant, stated independently of the
// implementation on purpose: if FastLevel::kTimeConstantSec moves, these
// assertions must fail rather than follow it silently.
constexpr double kTauSeconds = 0.015;
constexpr int kTauSamples = static_cast<int>(kTauSeconds * kRate);

std::vector<float> dc(float amp, int n) {
  return std::vector<float>(static_cast<std::size_t>(n), amp);
}

}  // namespace

TEST_CASE("G1 steady state reads the true mean square", "[fast_level]") {
  FastLevel lvl(kRate);
  const auto buf = dc(0.5f, kTauSamples * 20);  // 20 tau: fully settled
  lvl.process(buf.data(), buf.size());

  REQUIRE(lvl.mean_square() == Catch::Approx(0.25).margin(1e-6));
  REQUIRE(lvl.level_db() == Catch::Approx(-6.0206).margin(0.01));
}

TEST_CASE("G2 one time constant of a step reaches 1 - 1/e",
          "[fast_level]") {
  FastLevel lvl(kRate);
  REQUIRE(lvl.mean_square() == 0.0);  // fresh follower starts silent
  const auto buf = dc(1.0f, kTauSamples);
  lvl.process(buf.data(), buf.size());

  REQUIRE(lvl.mean_square() == Catch::Approx(1.0 - 1.0 / std::numbers::e)
                                    .margin(1e-3));
}

TEST_CASE("G3 release uses the same time constant", "[fast_level]") {
  FastLevel lvl(kRate);
  REQUIRE(lvl.mean_square() == 0.0);  // fresh follower starts silent
  const auto full = dc(1.0f, kTauSamples * 20);
  lvl.process(full.data(), full.size());
  REQUIRE(lvl.mean_square() == Catch::Approx(1.0).margin(1e-6));

  const auto quiet = dc(0.0f, kTauSamples);
  lvl.process(quiet.data(), quiet.size());

  REQUIRE(lvl.mean_square() ==
          Catch::Approx(1.0 / std::numbers::e).margin(1e-3));
}

TEST_CASE("G4 chunking does not change the trajectory", "[fast_level]") {
  const int n = 5000;
  std::vector<float> sig(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    sig[static_cast<std::size_t>(i)] = static_cast<float>(
        0.6 * std::sin(2.0 * std::numbers::pi * 440.0 * i / kRate));
  }

  FastLevel whole(kRate);
  whole.process(sig.data(), sig.size());

  FastLevel ragged(kRate);
  const int sizes[] = {1, 7, 64, 3, 997, 128, 2, 511};
  std::size_t at = 0;
  std::size_t k = 0;
  while (at < sig.size()) {
    std::size_t take = static_cast<std::size_t>(sizes[k % std::size(sizes)]);
    if (at + take > sig.size()) take = sig.size() - at;
    ragged.process(sig.data() + at, take);
    at += take;
    ++k;
  }

  REQUIRE(whole.mean_square() > 0.01);  // non-vacuous: real signal, real level
  REQUIRE(ragged.mean_square() ==
          Catch::Approx(whole.mean_square()).margin(1e-12));
}

TEST_CASE("G5 silence is finite and floored", "[fast_level]") {
  FastLevel lvl(kRate);
  const auto quiet = dc(0.0f, 1024);
  lvl.process(quiet.data(), quiet.size());

  REQUIRE(lvl.mean_square() == 0.0);
  REQUIRE(std::isfinite(lvl.level_db()));
  REQUIRE(lvl.level_db() <= -119.9);
}
