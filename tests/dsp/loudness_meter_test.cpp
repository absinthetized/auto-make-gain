// tests/dsp/loudness_meter_test.cpp — dual-metric kernel tests (RED phase).
//
// What is asserted (the specification — owner reviews before any real
// implementation exists, AGENTS.md rule 8):
//
//   B1. Golden LUFS: the standard's reference signal — a full-scale
//       (amp 1.0) mono 997 Hz sine — measures −3.0 LUFS ± 0.2 over a
//       3.0 s window. Derivation: sine mean square = 0.5; the
//       *standard's own 48 kHz Table 1 cascade* responds +0.691 dB at
//       997 Hz / +0.698 dB at 1000 Hz (verified numerically against the
//       verbatim table coefficients — the −0.691 constant in the LUFS
//       formula exists to cancel that real chain gain, which is why a
//       full-scale stereo reference sine reads 0.00 LKFS). Hence
//       L = −0.691 + 10·log10(0.5·10^0.0691) ≈ −3.01.
//   B2. Golden dB RMS: the SAME canned signal measures −3.0 dBFS ± 0.05
//       unweighted (10·log10 of the same mean square, no K gain).
//   B3. Sliding window: a window is the last N seconds regardless of
//       history — feed amp 0.5 for 5 s, then amp 1.0 for 3.2 s: both
//       metrics read the steady levels of amp 1.0 (old audio fully aged
//       out of the 3.0 s window).
//   B4. Metric cross-tie at the anchor (the FR-8 pairing property,
//       corrected while preparing the green): for the 997 Hz reference
//       tone, lufs - db_rms = -0.691 + 20*log10(chain_gain). With the
//       chain gain +0.691 dB at the anchor and the -0.691 loudness
//       constant, the difference is ~0 BY DESIGN — the two metrics
//       agree at the anchor (FS reference reads its dBFS level in both
//       scales). Asserting that agreement catches any systematic
//       offset in either metric.
//
// Method: the TESTS construct the meter with the exact training-window
// length (3.0 s at 48 kHz) and feed synthetic buffers in one call each.

#include "loudness_meter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using makegain::dsp::LoudnessMeter;
using makegain::dsp::LoudnessSnapshot;

namespace {

constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kRate = 48000.0;
constexpr double kWindow = 3.0; // s — the FR-2 training window

// Feed `seconds` of a sine at amp/freq into the meter, in `blocks` calls
// to also exercise arbitrary block sizes (worst case for accumulation).
void feed(LoudnessMeter& m, double seconds, double freq, double amp,
          int blocks) {
  const int n = static_cast<int>(seconds * kRate);
  const int per = n / blocks;
  std::vector<float> buf(per <= 0 ? 0 : static_cast<std::size_t>(per));
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < per; ++i) {
      const long long idx = static_cast<long long>(b) * per + i;
      buf[static_cast<std::size_t>(i)] =
          static_cast<float>(amp * std::sin(2.0 * kPi * freq *
                                            idx / kRate));
    }
    m.process(buf.data(), static_cast<std::size_t>(per));
  }
}

// Full-scale 997 Hz mono sine over a 3.0 s window at 48 kHz — the
// standard's reference signal, shared by B1..B4.
LoudnessSnapshot steady_reference() {
  LoudnessMeter m(kRate, kWindow);
  feed(m, 5.0, 997.0, 1.0, 7); // settle: window fully covered, ragged blocks
  return m.snapshot();
}

} // namespace

TEST_CASE("loudness: golden LUFS of the reference tone (B1)", "[loudness]") {
  const LoudnessSnapshot s = steady_reference();
  REQUIRE(s.valid);
  REQUIRE(s.lufs == Catch::Approx(-3.0).margin(0.2));
}

TEST_CASE("loudness: golden dB RMS of the reference tone (B2)", "[loudness]") {
  const LoudnessSnapshot s = steady_reference();
  REQUIRE(s.valid);
  REQUIRE(s.db_rms == Catch::Approx(-3.01).margin(0.05));
}

TEST_CASE("loudness: window slides with content (B3)", "[loudness]") {
  LoudnessMeter m(kRate, kWindow);
  feed(m, 5.0, 997.0, 0.5, 11);     // old, quieter content...
  feed(m, 3.2, 997.0, 1.0, 13);    // ...fully aged out; window is amp-1.0
  const LoudnessSnapshot s = m.snapshot();
  REQUIRE(s.valid);
  REQUIRE(s.lufs == Catch::Approx(-3.0).margin(0.2));
  REQUIRE(s.db_rms == Catch::Approx(-3.01).margin(0.05));
}

TEST_CASE("loudness: metric cross-tie at the anchor (B4)", "[loudness]") {
  const LoudnessSnapshot s = steady_reference();
  // Second spec correction (green prep, caught during derivation):
  // lufs - db_rms = -0.691 + 20*log10(chain_gain(f)). At the anchor
  // tone the chain gain is +0.691 dB, so the -0.691 constant cancels
  // it and BOTH METRICS AGREE within numerical noise — that agreement
  // is the calibration itself (FS reference reads its dBFS level in
  // both scales). Asserting a +0.69 gap here would contradict the
  // calibration the whole meter depends on.
  REQUIRE(std::fabs(s.lufs - s.db_rms) <= 0.05);
}
