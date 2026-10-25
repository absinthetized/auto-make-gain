// tests/dsp/gain_computer_test.cpp — the A2 delta-engine spec (RED
// phase; AGENTS.md rule 8: the owner reviews these assertions before any
// real implementation exists).
//
// Spec source: FR §10 (Amendment A2) + engine rows R-D1..R-D9 in
// doc/ai/m1-plan.md. The FSM-era cases (R1–R8, R-t1..t3, training, the
// reference codec) died with that spec and are NOT re-pinned here — the
// retired implementation lives in git history (PB1/PB2 commits).
//
// Case map:
//
//   R-D1  equal taps ⇒ zero delta; engine must not act (null invariant)
//   R-D2  program dynamics pass: equal level change in BOTH taps leaves
//         the applied gain untouched (identical meters ⇒ delta exactly 0)
//   R-D3  edit compensation: wet-only level change is nulled (±6 dB pair)
//   R-D4  metric pairing: LUFS vs dB RMS delta re-targets from the still-
//         running meters at the next slot (meter-as-oracle for values)
//   R-D5  clamp: |target| > MaxComp (16 dB default, A2) → applied AT the
//         bound, clamped flag exposed, both signs
//   R-D6  ramp: engage ramps to target (one-pole, closed-form window
//         check over the ≤100 ms slot take-over), disengage ramps home;
//         per-sample step bound never exceeded
//   R-D7  hold: EITHER tap silent → freeze at the last applied gain,
//         resume on validity; flag streaks: wet ≥ 5 s → no_signal_wet,
//         aux ≥ 5 s → dry_tap_missing; both clear again on validity
//   R-D8  chunk invariance at EVERY sample: ragged vs uniform feeds emit
//         bit-identical applied-gain trajectories (content has a
//         wet-silence burst, so the hold sits mid-trajectory)
//   R-D9  no memory beyond meters + parameters: disengage then re-engage
//         repeats the same compensation; nothing persists in secret
//
// Method: both taps are fed chunk-wise by ONE helper (each tap gets the
// same chunk size; R-D8 proves the per-sample property). Golden numbers
// use the 1 kHz-sine anchor: 0 dBFS ⇒ −3.0 LUFS, linear in level ⇒
// X dBFS ⇒ LUFS X−3.00. R-D4 uses a LoudnessMeter as the oracle so the
// test carries no hand-rolled K-weighting math.

#include "gain_computer.h"
#include "loudness_meter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

using Catch::Approx;

// The DSP layer's own namespace, unqualified for the tests.
namespace dsp = makegain::dsp;

namespace {

constexpr double kRate = 48000.0;

std::size_t seconds(double s) { return static_cast<std::size_t>(kRate * s); }

// 1 kHz sine at db dBFS, sample-index phase (deterministic).
std::vector<float> sine_1k(double db, std::size_t count) {
  const double amp = std::pow(10.0, db / 20.0);
  std::vector<float> v(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / kRate;
    v[i] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi
                                             * 1000.0 * t));
  }
  return v;
}

// Sine of a chosen frequency (R-D4's shelf-separated content).
std::vector<float> sine_at(double freq, double db, std::size_t count) {
  const double amp = std::pow(10.0, db / 20.0);
  std::vector<float> v(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / kRate;
    v[i] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * freq
                                             * t));
  }
  return v;
}

std::vector<float> digital_silence(std::size_t count) {
  return std::vector<float>(count, 0.0f);
}

// Feed both taps chunk-wise along a repeating pattern; write the
// applied-gain trajectory into `out` (same length, same order).
void feed(dsp::GainComputer& g, const std::vector<float>& wet,
          const std::vector<float>& dry, std::vector<float>& out,
          std::initializer_list<std::size_t> pattern) {
  const std::size_t n = wet.size();
  out.assign(n, -99.0f);  // trap: any position not written stays −99
  std::size_t pos = 0;
  auto it = pattern.begin();
  while (pos < n) {
    if (it == pattern.end()) it = pattern.begin();
    const std::size_t take = std::min(*it, n - pos);
    g.process(wet.data() + pos, dry.data() + pos, take, out.data() + pos);
    pos += take;
    ++it;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// R-D1 — null invariant: equal taps, no action.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: equal taps give zero delta — no chase (R-D1)") {
  const std::size_t n = seconds(4.5);
  const std::size_t settle = seconds(3.5);
  std::vector<float> wet = sine_1k(0.0, n);  // identical streams
  std::vector<float> dry = wet;

  dsp::GainComputer g(kRate);
  g.set_engaged(true);
  std::vector<float> out;
  feed(g, wet, dry, out, {7u, 11u, 13u, 1u, 479u});

  // Emission covered every sample (no trap values at the tail).
  for (std::size_t i = n - 11; i < n; ++i) CHECK(out[i] != -99.0f);

  CHECK(g.delta_valid());
  CHECK(g.applied_gain_db() == Approx(0.0).margin(0.01));
  CHECK(g.delta_db() == Approx(0.0).margin(1e-9));
  double worst = 0.0;
  for (std::size_t i = settle; i < n; ++i)
    worst = std::max(worst, std::abs(static_cast<double>(out[i])));
  CHECK(worst < 0.01);
  CHECK(g.flags().clamped == false);
  CHECK(g.flags().no_signal_wet == false);
  CHECK(g.flags().dry_tap_missing == false);
}

// ---------------------------------------------------------------------------
// R-D2 — program dynamics pass: equal change in both taps, no gain move.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: program dynamics pass through (R-D2)") {
  const std::size_t n1 = seconds(3.0);
  const std::size_t n2 = seconds(1.0);
  const std::size_t n = n1 + n2;
  // BOTH taps carry the level change (the program changed; the chain
  // did not): the whole point of R-D2 is that such equal motion is NOT
  // chased. (PB2e first-run lesson: an unchanged `dry` here would make
  // it a REAL edit — a −6 dB delta — and the engine was right to chase.)
  const auto dry = [&] {
    std::vector<float> d = sine_1k(0.0, n1);
    const auto tail = sine_1k(-6.0, n2);
    d.insert(d.end(), tail.begin(), tail.end());
    return d;
  }();
  std::vector<float> wet = sine_1k(0.0, n1);
  const auto tail = sine_1k(-6.0, n2);
  wet.insert(wet.end(), tail.begin(), tail.end());

  dsp::GainComputer g(kRate);
  g.set_engaged(true);
  std::vector<float> out;
  feed(g, wet, dry, out, {13u, 1u, 479u});

  // Identical meters consume identical samples: the delta is exactly
  // 0.0 through the level jump — the gain must not be fooled into
  // chasing the transition (the retired leveler chased by design).
  CHECK(g.delta_db() == Approx(0.0).margin(1e-9));
  CHECK(g.applied_gain_db() == Approx(0.0).margin(0.01));
  double worst = 0.0;
  for (std::size_t i = n1; i < n; ++i)
    worst = std::max(worst, std::abs(static_cast<double>(out[i])));
  CHECK(worst < 0.05);
  CHECK(g.flags().clamped == false);
}

// ---------------------------------------------------------------------------
// R-D3 — edit compensation: wet-only level changes are nulled.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: chain edits are compensated (R-D3)") {
  // 3.0 s to fill the meter windows + 3.0 s ramp: settled value within
  // the 0.05 margin of the target (1−e^{−10} ≈ 1.0).
  const std::size_t n = seconds(6.0);

  SECTION("wet 6 dB quieter than dry → +6 dB compensation") {
    const auto dry = sine_1k(0.0, n);   // −3.0 LUFS
    const auto wet = sine_1k(-6.0, n);  // −9.0 LUFS ⇒ delta −6 ⇒ +6
    dsp::GainComputer g(kRate);
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, wet, dry, out, {7u, 1u, 479u, 13u});
    CHECK(g.delta_db() == Approx(-6.0).margin(0.05));
    CHECK(g.applied_gain_db() == Approx(6.0).margin(0.05));
    CHECK(g.flags().clamped == false);
  }
  SECTION("wet 6 dB louder than dry → −6 dB compensation") {
    const auto dry = sine_1k(-6.0, n);
    const auto wet = sine_1k(0.0, n);
    dsp::GainComputer g(kRate);
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, wet, dry, out, {7u, 1u, 479u, 13u});
    CHECK(g.delta_db() == Approx(6.0).margin(0.05));
    CHECK(g.applied_gain_db() == Approx(-6.0).margin(0.05));
    CHECK(g.flags().clamped == false);
  }
}

// ---------------------------------------------------------------------------
// R-D4 — metric pairing: re-target from the running meters at the next
// slot; the LoudnessMeter itself is the oracle (no hand K-weighting).
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: metric switch re-targets from the same meters (R-D4)") {
  // Same settle arithmetic as R-D3: 3.0 s window fill + 3.0 s ramp.
  const std::size_t n = seconds(6.0);
  // Shelf-separated content: the K high shelf splits LUFS, unweighted
  // dB RMS stays (nearly) equal — the pairing choice is visibly
  // different without hand-computing filter math.
  const auto dry = sine_at(1000.0, 0.0, n);
  const auto wet = sine_at(6000.0, 0.0, n);

  // Oracle: the same meter engine, same content, same window.
  dsp::LoudnessMeter m_wet(kRate, 3.0);
  dsp::LoudnessMeter m_dry(kRate, 3.0);
  m_wet.process(wet.data(), n);
  m_dry.process(dry.data(), n);
  const auto s_wet = m_wet.snapshot();
  const auto s_dry = m_dry.snapshot();
  CHECK(s_wet.valid);
  CHECK(s_dry.valid);
  const double delta_lufs = s_wet.lufs - s_dry.lufs;
  const double delta_rms = s_wet.db_rms - s_dry.db_rms;
  CHECK(delta_rms == Approx(0.0).margin(0.1));  // RMS: equal by design
  CHECK(delta_lufs > 0.5);                       // shelf region: 6 kHz louder

  dsp::GainComputer g(kRate);
  g.set_engaged(true);
  std::vector<float> out;  // metric default = LUFS
  feed(g, wet, dry, out, {479u, 13u});
  CHECK(g.applied_gain_db() == Approx(-delta_lufs).margin(0.05));

  // Switch to dB RMS: the next slot re-targets from the SAME running
  // meters (the second feed runs the full 6 s scenario again; plenty).
  g.set_metric(dsp::GainMetric::db_rms);
  feed(g, wet, dry, out, {479u, 13u});
  CHECK(g.applied_gain_db() == Approx(-delta_rms).margin(0.05));
}

// ---------------------------------------------------------------------------
// R-D5 — clamp both signs (A2 default MaxComp 16 dB).
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: clamp at MaxComp with the flag (R-D5)") {
  const std::size_t n = seconds(6.5);  // 3.0 fill + 3.5 ramp-decay

  SECTION("target +24 clamps to +16") {
    const auto dry = sine_1k(0.0, n);    // −3.0 LUFS
    const auto wet = sine_1k(-24.0, n);  // −27.0 ⇒ delta −24 ⇒ target +24
    dsp::GainComputer g(kRate);
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, wet, dry, out, {479u});
    CHECK(g.applied_gain_db() == Approx(16.0).margin(0.05));
    CHECK(g.flags().clamped == true);
  }
  SECTION("target −24 clamps to −16") {
    const auto dry = sine_1k(-24.0, n);
    const auto wet = sine_1k(0.0, n);
    dsp::GainComputer g(kRate);
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, wet, dry, out, {479u});
    CHECK(g.applied_gain_db() == Approx(-16.0).margin(0.05));
    CHECK(g.flags().clamped == true);
  }
}

// ---------------------------------------------------------------------------
// R-D6 — ramp: no slam on engage, one-pole closed form, ramp home on
// disengage; the per-sample step bound is never exceeded.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: ramp properties on engage/disengage (R-D6)") {
  const std::size_t settle = seconds(3.5);
  const auto dry = sine_1k(0.0, seconds(9.5));    // −3.0 LUFS
  const auto wet = sine_1k(-6.0, seconds(9.5));   // −9.0 ⇒ target +6

  dsp::GainComputer g(kRate);  // NOT engaged: target stays 0, applied 0
  std::vector<float> pre;
  feed(g, wet, dry, pre, {7u, 1u, 479u, 13u});  // settle pre-engage
  CHECK(g.applied_gain_db() == Approx(0.0).margin(0.01));

  // Engage. The target takes over at the NEXT internal slot (≤ 100 ms),
  // so the closed-form check tolerates a 0.4–0.5 s ramp-age window at
  // the 0.5 s mark after the engage call.
  g.set_engaged(true);
  std::vector<float> out;
  feed(g, wet, dry, out, {13u, 479u, 7u});  // exactly 3.0 s post-engage
  const double alpha = 1.0 - std::exp(-1.0 / (kRate * 0.3));
  const double lo = 6.0 * (1.0 - std::exp(-0.4 / 0.3));  // ramp-age 0.4 s
  const double hi = 6.0 * (1.0 - std::exp(-0.5 / 0.3));  // ramp-age 0.5 s
  const double at_half =
      static_cast<double>(out.at(seconds(0.5) - 1));
  CHECK(at_half >= Approx(lo).margin(0.3));
  CHECK(at_half <= Approx(hi).margin(0.3));
  // Fully settled by the end (≈ target; 1−e^{−9.7} ≈ 1).
  CHECK(out.back() == Approx(6.0).margin(0.05));

  // Step bound: the one-pole pace must hold at every sample.
  double worst_step = 0.0;
  for (std::size_t i = 1; i < out.size(); ++i)
    worst_step = std::max(
        worst_step, std::abs(static_cast<double>(out[i])
                             - static_cast<double>(out[i - 1])));
  CHECK(worst_step <= 6.0 * alpha + 1e-5);

  // Disengage: ramp home (3.0 s: < 0.001 dB left of the decay).
  g.set_engaged(false);
  std::vector<float> home;
  feed(g, wet, dry, home, {479u, 13u});  // exactly 3.0 s
  CHECK(home.back() == Approx(0.0).margin(0.1));
}

// ---------------------------------------------------------------------------
// R-D7 — hold: either-tap silence freezes the chase; flag streaks.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: silence holds and flags report taps (R-D7)") {
  SECTION("wet silence: gain frozen, no chase; streak lights past 5 s; clears") {
    const std::size_t settle = seconds(5.0);  // done decaying by ~4.5 s
    const auto dry = sine_1k(0.0, settle + seconds(2.2));

    // Short silence first: below the 5 s streak, flag must stay dark.
    std::vector<float> wet = sine_1k(-6.0, settle);
    { const auto gap = digital_silence(seconds(2.2));
      wet.insert(wet.end(), gap.begin(), gap.end()); }
    dsp::GainComputer g(kRate);
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, wet, dry, out, {7u, 1u, 479u, 13u});
    CHECK(g.applied_gain_db() == Approx(6.0).margin(0.05));  // settled pre-silence
    const double frozen = out.at(settle + seconds(0.7));
    CHECK(frozen == Approx(6.0).margin(0.05));   // no chase into silence
    CHECK(out.back() == Approx(6.0).margin(0.05));  // still held at the end
    CHECK(g.flags().no_signal_wet == false);   // only 2.2 s < 5 s
    CHECK(g.flags().dry_tap_missing == false); // dry never silent

    // Long silence on a fresh engine: the streak must light. The dry
    // tap spans the full scenario length (never silent).
    std::vector<float> wet_long = sine_1k(-6.0, settle);
    { const auto gap = digital_silence(seconds(5.4));
      wet_long.insert(wet_long.end(), gap.begin(), gap.end()); }
    dsp::GainComputer g2(kRate);
    g2.set_engaged(true);
    std::vector<float> out2;
    const auto dry_long = sine_1k(0.0, settle + seconds(5.4));
    feed(g2, wet_long, dry_long, out2, {479u, 13u});
    CHECK(g2.flags().no_signal_wet == true);
    CHECK(g2.flags().dry_tap_missing == false);
    CHECK(g2.applied_gain_db() == Approx(6.0).margin(0.05));  // held, not reset
    // Validity returns → the flag clears on the next slot.
    std::vector<float> dry3 = sine_1k(0.0, seconds(0.5));
    std::vector<float> wet3 = sine_1k(-6.0, seconds(0.5));
    std::vector<float> out3;
    feed(g2, wet3, dry3, out3, {13u});
    CHECK(g2.flags().no_signal_wet == false);
  }

  SECTION("aux silence: gain frozen; dry streak lights past 5 s; clears") {
    const std::size_t settle = seconds(5.0);  // holds a settled +6
    const std::size_t n = settle + seconds(5.4);  // streak > 5 s
    const auto wet = sine_1k(-6.0, n);          // never silent
    std::vector<float> dry = sine_1k(0.0, settle);
    { const auto gap = digital_silence(seconds(5.4));
      dry.insert(dry.end(), gap.begin(), gap.end()); }

    dsp::GainComputer g(kRate);
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, wet, dry, out, {7u, 1u, 479u, 13u});
    // Held where the settle left it — the gain neither chases nor drifts
    // while the delta is invalid (this is the whole point of the hold).
    const double frozen = out.at(settle + seconds(0.7));
    CHECK(frozen == Approx(out.at(settle - 1)).margin(0.02));
    CHECK(g.flags().dry_tap_missing == true);   // aux silent ≥ 5 s
    CHECK(g.flags().no_signal_wet == false);
    // Streak clears the moment real dry audio is back.
    std::vector<float> dry5 = sine_1k(0.0, seconds(0.5));
    std::vector<float> wet5 = sine_1k(-6.0, seconds(0.5));
    std::vector<float> out5;
    feed(g, wet5, dry5, out5, {13u});
    CHECK(g.flags().dry_tap_missing == false);
  }
}

// ---------------------------------------------------------------------------
// R-D8 — chunk invariance at every sample (hold sits mid-trajectory).
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: chunk invariance of the emitted trajectory (R-D8)") {
  const std::size_t a = seconds(3.0);
  const std::size_t gap_n = seconds(0.5);
  // 4.0 s of resume: the first ~1 s the window still contains the tail
  // of the silence burst, so the delta (and the target) transiently
  // OVERSHOOTS; final assertion waits for that drain to settle out.
  const std::size_t back_n = seconds(4.0);
  const std::size_t n = a + gap_n + back_n;
  std::vector<float> dry = sine_1k(0.0, n);
  std::vector<float> wet = sine_1k(-6.0, a);
  { const auto gap = digital_silence(gap_n); wet.insert(wet.end(), gap.begin(), gap.end()); }
  { const auto back = sine_1k(-6.0, back_n); wet.insert(wet.end(), back.begin(), back.end()); }

  dsp::GainComputer g_ragged(kRate);
  g_ragged.set_engaged(true);
  std::vector<float> out_ragged;
  feed(g_ragged, wet, dry, out_ragged, {7u, 11u, 13u, 1u, 479u});

  dsp::GainComputer g_uniform(kRate);
  g_uniform.set_engaged(true);
  std::vector<float> out_uniform;
  feed(g_uniform, wet, dry, out_uniform, {48000u});

  // Bit-identical trajectories regardless of blocking — every sample,
  // including through the silence hold and the resume.
  CHECK(out_ragged == out_uniform);
  CHECK(out_uniform.back() == Approx(6.0).margin(0.05));  // resumed
}

// ---------------------------------------------------------------------------
// R-D9 — no memory beyond meters + parameters.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: re-engage repeats the compensation, nothing hidden (R-D9)") {
  const std::size_t settle = seconds(5.0);              // fully settled first
  const std::size_t recoup = seconds(3.0);              // ramp phase room
  const std::size_t n = settle + 2 * recoup;
  const auto wet = sine_1k(-6.0, n);
  const auto dry = sine_1k(0.0, n);

  dsp::GainComputer g(kRate);
  g.set_engaged(true);
  std::vector<float> out1;
  feed(g, wet, dry, out1, {7u, 1u, 479u, 13u});  // settle phase:
  const double first = g.applied_gain_db();
  CHECK(first == Approx(6.0).margin(0.05));

  g.set_engaged(false);
  std::vector<float> out2;
  feed(g, wet, dry, out2, {479u, 13u});  // 3.0 s ramp home
  const double down = g.applied_gain_db();
  CHECK(down == Approx(0.0).margin(0.1));

  g.set_engaged(true);
  std::vector<float> out3;
  feed(g, wet, dry, out3, {479u, 13u});  // re-settle phase:
  const double again = g.applied_gain_db();
  CHECK(again == Approx(6.0).margin(0.05));
  CHECK(g.delta_db() == Approx(-6.0).margin(0.05));  // unchanged by the cycle
}

// ---------------------------------------------------------------------------
// Amendment A3 (PR-C) — R-F1..R-F8: freeze, meter window (response),
// metering HP. RED phase: the stubs compile but cannot satisfy these.
// ---------------------------------------------------------------------------

namespace {

// Deterministic white-noise-ish stream (seeded LCG), [-1, 1).
std::vector<float> noise(std::size_t count, unsigned seed = 7) {
  std::vector<float> v(count);
  std::uint32_t x = seed << 8 | 1;
  for (std::size_t i = 0; i < count; ++i) {
    x = x * 1664525u + 1013904223u;
    v[i] = static_cast<float>(static_cast<double>(x >> 8) / 8388608.0 - 1.0);
  }
  return v;
}

// First index (in samples) from which `out` stays within `tol` of
// `goal` for the remainder — trivial "settle time" measure for R-F4.
std::size_t settle_index(const std::vector<float>& out, double goal,
                         double tol) {
  for (std::size_t i = 0; i < out.size(); ++i) {
    bool ok = true;
    for (std::size_t j = i; j < out.size(); ++j)
      if (std::abs(static_cast<double>(out[j]) - goal) > tol) { ok = false; break; }
    if (ok) return i;
  }
  return out.size();
}

}  // namespace

// R-F1 — freeze locks the applied gain completely; the state is exposed.
TEST_CASE("gain computer: freeze locks applied gain (R-F1)") {
  const std::size_t n = seconds(6.0);
  // Both taps live: wet exactly 6 dB louder than dry => settled target −6.
  std::vector<float> dry = sine_1k(0.0, n);
  std::vector<float> wet = sine_1k(6.02, n);
  dsp::GainComputer g(kRate);
  g.set_engaged(true);

  std::vector<float> pre;
  feed(g, wet, dry, pre, {7u, 1u, 479u, 13u});    // settle
  const double settled = g.applied_gain_db();
  CHECK(settled == Approx(-6.0).margin(0.05));

  g.set_frozen(true);
  CHECK(g.frozen());

  // Frozen: content changes must not move the gain one whit.
  const std::size_t n2 = seconds(2.0);
  std::vector<float> wet2 = sine_1k(-12.0, n2);
  std::vector<float> dry2(n2, 0.0f);
  std::vector<float> out;
  feed(g, wet2, dry2, out, {7u, 1u, 479u, 13u});
  for (std::size_t i = 0; i < out.size(); ++i)
    CHECK(out[i] == Approx(settled).margin(1e-6));
}

// R-F2 — unfreeze is continuous: no jump, ramp resumes from held value.
TEST_CASE("gain computer: unfreeze resumes from the held value (R-F2)") {
  const std::size_t n = seconds(6.0);
  std::vector<float> wet = sine_1k(-6.0, n);   // wet quieter → +6 compensation
  std::vector<float> dry = sine_1k(0.0, n);
  dsp::GainComputer g(kRate);
  g.set_ramp_tc(0.03);
  g.set_engaged(true);

  std::vector<float> pre;
  feed(g, wet, dry, pre, {7u, 1u, 479u, 13u});
  const double settled = g.applied_gain_db();
  CHECK(settled == Approx(6.0).margin(0.05));

  g.set_frozen(true);
  std::vector<float> held;
  feed(g, wet, dry, held, {479u, 13u});        // 1 s frozen, static

  // Unfreeze into a NEW edit (wet drops a further 6 dB). First sample
  // after release equals the held value exactly; thereafter the gain
  // moves (resumed evaluation) without a jump.
  g.set_frozen(false);
  std::vector<float> wet3 = sine_1k(-12.0, seconds(2.0));
  std::vector<float> dry3(dry.end() - wet3.size(), dry.end());  // dry tail
  std::vector<float> post;
  feed(g, wet3, dry3, post, {479u, 13u});
  CHECK(post[0] == Approx(held.back()).margin(1e-6));
  const double later = g.applied_gain_db();
  CHECK(later > settled + 1.0);                // resumed toward ~+12
}

// R-F3 — response is a measurement change, not a target change: Short
// and Fast-windows engines converge to the SAME steady delta.
TEST_CASE("gain computer: Fast and Short converge to the same delta (R-F3)") {
  const std::size_t n = seconds(7.0);
  std::vector<float> wet = sine_1k(-6.0, n);
  std::vector<float> dry = sine_1k(0.0, n);

  dsp::GainComputer g_short(kRate);
  g_short.set_engaged(true);
  std::vector<float> out_s;
  feed(g_short, wet, dry, out_s, {7u, 1u, 479u, 13u});

  dsp::GainComputer g_fast(kRate);
  g_fast.set_engaged(true);
  g_fast.set_meter_window(0.4);
  std::vector<float> out_f;
  feed(g_fast, wet, dry, out_f, {7u, 1u, 479u, 13u});

  CHECK(g_fast.delta_valid());
  CHECK(g_fast.delta_db() == Approx(g_short.delta_db()).margin(0.05));
  CHECK(g_fast.applied_gain_db() == Approx(6.0).margin(0.05));
}

// R-F4 — the actual point of Fast: it finishes converging much sooner.
// Setup: both meters settle on delta-0 content, engaged; at the edit the
// WET tap alone drops 6 dB (an equal-taps → edited-taps transition), so
// the target climbs to +6 exactly as fast as the meter window lets it.
TEST_CASE("gain computer: Fast settles far sooner than Short (R-F4)") {
  // Pre-edit: 3.5 s of equal taps (delta 0), engines engaged so meters
  // fill AND the applied gain sits at the settled 0 dB.
  const std::size_t pre_n = seconds(3.5);
  const std::size_t post_n = seconds(3.0);
  std::vector<float> pre_wet = sine_1k(0.0, pre_n);
  std::vector<float> pre_dry = sine_1k(0.0, pre_n);
  // Post-edit: wet alone quieter by 6 dB.
  const std::vector<float> post_wet = sine_1k(-6.0, post_n);
  const std::vector<float> post_dry = sine_1k(0.0, post_n);

  dsp::GainComputer g_short(kRate);
  g_short.set_ramp_tc(0.03);
  g_short.set_engaged(true);
  {
    std::vector<float> tmp;
    feed(g_short, pre_wet, pre_dry, tmp, {479u});
  }
  std::vector<float> out_s;
  feed(g_short, post_wet, post_dry, out_s, {479u});

  dsp::GainComputer g_fast(kRate);
  g_fast.set_ramp_tc(0.03);
  g_fast.set_engaged(true);
  g_fast.set_meter_window(0.4);
  {
    std::vector<float> tmp;
    feed(g_fast, pre_wet, pre_dry, tmp, {479u});
  }
  std::vector<float> out_f;
  feed(g_fast, post_wet, post_dry, out_f, {479u});

  // out_s / out_f start AT the edit; settle time is measured from there.
  const double fast_settle = static_cast<double>(
      settle_index(out_f, 6.0, 0.05)) / kRate;
  const double short_settle = static_cast<double>(
      settle_index(out_s, 6.0, 0.05)) / kRate;
  CHECK(fast_settle < 1.0);
  CHECK(short_settle > 1.8);
}

// R-F5 — metering HP at its own cutoff: −3 dB insertion (Butterworth
// flatness point), measured through the engine's per-tap loudness
// readout (oracle = the engine itself, no hand-rolled weighting).
TEST_CASE("gain computer: meter HP reads -3 dB at cutoff (R-F5)") {
  const std::size_t n = seconds(2.5);
  const std::vector<float> tone = sine_at(150.0, 0.0, n);
  const std::vector<float> silence(n, 0.0f);

  dsp::GainComputer g_off(kRate);
  std::vector<float> out;
  feed(g_off, tone, silence, out, {479u, 13u});
  dsp::GainComputer g_on(kRate);
  g_on.set_meter_hp(true, 150.0);
  std::vector<float> out2;
  feed(g_on, tone, silence, out2, {479u, 13u});

  const double loss = g_on.wet_loudness_db() - g_off.wet_loudness_db();
  CHECK(loss == Approx(-3.0).margin(0.5));
}

// R-F6 — the slope is 24 dB/oct: one octave BELOW cutoff, the passed
// energy drops ~24 dB (12 dB/oct would read ~half of that).
TEST_CASE("gain computer: meter HP slope is 24 dB/oct (R-F6)") {
  const std::size_t n = seconds(2.5);
  const std::vector<float> tone = sine_at(75.0, 0.0, n);
  const std::vector<float> silence(n, 0.0f);

  dsp::GainComputer g_off(kRate);
  std::vector<float> out;
  feed(g_off, tone, silence, out, {479u, 13u});
  dsp::GainComputer g_on(kRate);
  g_on.set_meter_hp(true, 150.0);
  std::vector<float> out2;
  feed(g_on, tone, silence, out2, {479u, 13u});

  const double loss = g_on.wet_loudness_db() - g_off.wet_loudness_db();
  CHECK(loss < -60.0);   // ≈ −24 dB + far-below-cutoff content ≈ inaudible floor
}

// R-F7 — symmetric filtering of equal taps stays EXACTLY zero: the
// musical-motion pass-through invariant survives the metering HP.
TEST_CASE("gain computer: meter HP keeps the null invariant (R-F7)") {
  const std::size_t n = seconds(3.0);
  const std::vector<float> program = noise(seconds(1.0));
  std::vector<float> wet = noise(n);
  std::vector<float> dry = wet;

  dsp::GainComputer g(kRate);
  g.set_meter_hp(true, 150.0);
  g.set_engaged(true);
  std::vector<float> out;
  feed(g, wet, dry, out, {7u, 1u, 479u, 13u});

  CHECK(g.delta_valid());
  CHECK(g.delta_db() == Approx(0.0).margin(1e-9));
  CHECK(g.applied_gain_db() == Approx(0.0).margin(0.01));
}

// R-F8 — HP flag off (stored default) is byte-identical to today's
// engine: the feature is opt-in, the default behavior is untouched.
TEST_CASE("gain computer: HP off is passthrough to the A2 engine (R-F8)") {
  const std::size_t n = seconds(6.0);
  std::vector<float> wet = sine_1k(-6.0, n);
  std::vector<float> dry = sine_1k(0.0, n);

  dsp::GainComputer g_plain(kRate);
  g_plain.set_engaged(true);
  std::vector<float> out_plain;
  feed(g_plain, wet, dry, out_plain, {7u, 1u, 479u, 13u});

  dsp::GainComputer g_explicit(kRate);
  g_explicit.set_engaged(true);
  g_explicit.set_meter_hp(false, 150.0);
  std::vector<float> out_exp;
  feed(g_explicit, wet, dry, out_exp, {7u, 1u, 479u, 13u});

  for (std::size_t i = 0; i < n; ++i)
    CHECK(out_exp[i] == Approx(out_plain[i]).margin(1e-12));
}
