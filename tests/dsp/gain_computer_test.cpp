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
//   R-F1  freeze locks the applied gain; the state is exposed
//   R-F2  unfreeze is continuous: no jump, ramp resumes from held value
//   R-F5..F8 metering HP: −3 dB at cutoff, 24 dB/oct slope, null
//         invariant under the filter, off = the A2 engine unchanged
//
//   R-G1  Fast converges a chain edit inside 200 ms (Smooth does not)
//   R-G2  floor rule: Fast corner = max(HP, 80) with 80 when off;
//         Smooth has no floor
//   R-G3  mode switch mid-run: both detectors warm, target re-aims at
//         the next evaluation, ramp carries it (no gap, no slam)
//   R-G4  silence hold + flag are time-based (5 s) on both grids
//   R-G5  chunk invariance on the 5 ms grid; first response ≤ 10 ms
//   R-G6  identical taps stay an exact 0 dB delta in BOTH modes
//   R-G7  mode change refused while frozen (FR-30), accepted after
//
// R-G8 (the interim Metric/window shim) retired in C7 with the shim
// itself; the Fast default it also checked is pinned by R-G1.
//
// The A2/A3 cases pin the SMOOTH detector explicitly (smooth_engine):
// Smooth keeps today's stage exactly (FR-28). R-D4 retired into R-G3
// (metric pairing became the mode switch); R-F3/R-F4 (the Response
// window) retired into R-G1/R-G3.
//
// Method: both taps are fed chunk-wise by ONE helper (each tap gets the
// same chunk size; R-D8 proves the per-sample property). Golden numbers
// use the 1 kHz-sine anchor: 0 dBFS ⇒ −3.0 LUFS, linear in level ⇒
// X dBFS ⇒ LUFS X−3.00 (and ⇒ X−3.01 dB in Fast's level reading, close
// enough that the anchor carries). R-G3 uses a LoudnessMeter as the
// oracle so the test carries no hand-rolled K-weighting math.

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

// Legacy A2/A3 cases below pin the SMOOTH detector — the 3 s LUFS
// window and 100 ms grid they were written against (FR-28 keeps
// Smooth's stage exactly as it was). Fast is the default now and has
// its own cases R-G1..R-G7 at the end of the file.
dsp::GainComputer smooth_engine(double rate = kRate) {
  dsp::GainComputer g(rate);
  g.set_mode(dsp::GainMode::smooth);
  return g;
}

// ---------------------------------------------------------------------------
// R-D1 — null invariant: equal taps, no action.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: equal taps give zero delta — no chase (R-D1)") {
  const std::size_t n = seconds(4.5);
  const std::size_t settle = seconds(3.5);
  std::vector<float> wet = sine_1k(0.0, n);  // identical streams
  std::vector<float> dry = wet;

  dsp::GainComputer g = smooth_engine();
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

  dsp::GainComputer g = smooth_engine();
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
    dsp::GainComputer g = smooth_engine();
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
    dsp::GainComputer g = smooth_engine();
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, wet, dry, out, {7u, 1u, 479u, 13u});
    CHECK(g.delta_db() == Approx(6.0).margin(0.05));
    CHECK(g.applied_gain_db() == Approx(-6.0).margin(0.05));
    CHECK(g.flags().clamped == false);
  }
}

// ---------------------------------------------------------------------------
// R-D5 — clamp both signs (A2 default MaxComp 16 dB).
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: clamp at MaxComp with the flag (R-D5)") {
  const std::size_t n = seconds(6.5);  // 3.0 fill + 3.5 ramp-decay

  SECTION("target +24 clamps to +16") {
    const auto dry = sine_1k(0.0, n);    // −3.0 LUFS
    const auto wet = sine_1k(-24.0, n);  // −27.0 ⇒ delta −24 ⇒ target +24
    dsp::GainComputer g = smooth_engine();
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, wet, dry, out, {479u});
    CHECK(g.applied_gain_db() == Approx(16.0).margin(0.05));
    CHECK(g.flags().clamped == true);
  }
  SECTION("target −24 clamps to −16") {
    const auto dry = sine_1k(-24.0, n);
    const auto wet = sine_1k(0.0, n);
    dsp::GainComputer g = smooth_engine();
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
  const auto dry = sine_1k(0.0, seconds(9.5));    // −3.0 LUFS
  const auto wet = sine_1k(-6.0, seconds(9.5));   // −9.0 ⇒ target +6

  dsp::GainComputer g = smooth_engine();  // NOT engaged: target stays 0, applied 0
  // A3 note: the default tc changed to 50 ms (FR-25). This case pins
  // the ONE-POLE SHAPE, not the default: pin tc = 0.3 explicitly so the
  // closed-form bounds keep asserting exactly that shape.
  g.set_ramp_tc(0.3);
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
    const std::size_t gap_n = seconds(2.2);
    std::vector<float> wet = sine_1k(-6.0, settle);
    wet.reserve(settle + gap_n);
    { const auto gap = digital_silence(gap_n);
      wet.insert(wet.end(), gap.begin(), gap.end()); }
    dsp::GainComputer g = smooth_engine();
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
    dsp::GainComputer g2 = smooth_engine();
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

    dsp::GainComputer g = smooth_engine();
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
  wet.reserve(n);
  { const auto gap = digital_silence(gap_n); wet.insert(wet.end(), gap.begin(), gap.end()); }
  { const auto back = sine_1k(-6.0, back_n); wet.insert(wet.end(), back.begin(), back.end()); }

  dsp::GainComputer g_ragged = smooth_engine();
  g_ragged.set_engaged(true);
  std::vector<float> out_ragged;
  feed(g_ragged, wet, dry, out_ragged, {7u, 11u, 13u, 1u, 479u});

  dsp::GainComputer g_uniform = smooth_engine();
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

  dsp::GainComputer g = smooth_engine();
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

}  // namespace

// R-F1 — freeze locks the applied gain completely; the state is exposed.
TEST_CASE("gain computer: freeze locks applied gain (R-F1)") {
  const std::size_t n = seconds(6.0);
  // Both taps live: wet exactly 6 dB louder than dry => settled target −6.
  std::vector<float> dry = sine_1k(0.0, n);
  std::vector<float> wet = sine_1k(6.02, n);
  dsp::GainComputer g = smooth_engine();
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
  dsp::GainComputer g = smooth_engine();
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

// R-F5 — metering HP at its own cutoff: −3 dB insertion (Butterworth
// flatness point), measured through the engine's per-tap loudness
// readout (oracle = the engine itself, no hand-rolled weighting).
// Content length 4.0 s > the 3 s meter window: snapshots must be VALID.
TEST_CASE("gain computer: meter HP reads -3 dB at cutoff (R-F5)") {
  const std::size_t n = seconds(4.0);
  const std::vector<float> tone = sine_at(150.0, 0.0, n);
  const std::vector<float> silence(n, 0.0f);

  dsp::GainComputer g_off = smooth_engine();
  std::vector<float> out;
  feed(g_off, tone, silence, out, {479u, 13u});
  dsp::GainComputer g_on = smooth_engine();
  g_on.set_meter_hp(true, 150.0);
  std::vector<float> out2;
  feed(g_on, tone, silence, out2, {479u, 13u});

  const double loss = g_on.wet_loudness_db() - g_off.wet_loudness_db();
  CHECK(loss == Approx(-3.0).margin(0.5));
}

// R-F6 — the slope is 24 dB/oct: one octave BELOW cutoff, the passed
// energy drops ~24 dB (12 dB/oct would read ~half of that).
TEST_CASE("gain computer: meter HP slope is 24 dB/oct (R-F6)") {
  // One octave BELOW cutoff (75 Hz at fc = 150): 4th-order Butterworth
  // attenuates about -24.09 dB; a 12 dB/oct would leave about -12 (half).
  // Content 4.0 s > the meter window so the readouts are valid.
  const std::size_t n = seconds(4.0);
  const std::vector<float> tone = sine_at(75.0, 0.0, n);
  const std::vector<float> silence(n, 0.0f);

  dsp::GainComputer g_off = smooth_engine();
  std::vector<float> out;
  feed(g_off, tone, silence, out, {479u, 13u});
  dsp::GainComputer g_on = smooth_engine();
  g_on.set_meter_hp(true, 150.0);
  std::vector<float> out2;
  feed(g_on, tone, silence, out2, {479u, 13u});

  const double loss = g_on.wet_loudness_db() - g_off.wet_loudness_db();
  CHECK(loss == Approx(-24.0).margin(1.5));   // ≈ −24.1 dB, far from −12
}

// R-F7 — symmetric filtering of equal taps stays EXACTLY zero: the
// musical-motion pass-through invariant survives the metering HP.
TEST_CASE("gain computer: meter HP keeps the null invariant (R-F7)") {
  const std::size_t n = seconds(3.0);
  const std::vector<float> program = noise(seconds(1.0));
  std::vector<float> wet = noise(n);
  std::vector<float> dry = wet;

  dsp::GainComputer g = smooth_engine();
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

  dsp::GainComputer g_plain = smooth_engine();
  g_plain.set_engaged(true);
  std::vector<float> out_plain;
  feed(g_plain, wet, dry, out_plain, {7u, 1u, 479u, 13u});

  dsp::GainComputer g_explicit = smooth_engine();
  g_explicit.set_engaged(true);
  g_explicit.set_meter_hp(false, 150.0);
  std::vector<float> out_exp;
  feed(g_explicit, wet, dry, out_exp, {7u, 1u, 479u, 13u});

  for (std::size_t i = 0; i < n; ++i)
    CHECK(out_exp[i] == Approx(out_plain[i]).margin(1e-12));
}

// ===========================================================================
// A5 (M3b) — R-G1..R-G7: the two compensation modes (FR-28/29/30).
// The Fast-path cases fail against the C5 stub and pass against the real
// engine: they are the guard the mode split was built to satisfy.
// ===========================================================================

// ---------------------------------------------------------------------------
// R-G1 — Fast settles a chain edit inside 200 ms; Smooth needs seconds.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: Fast settles a chain edit inside 200 ms (R-G1)") {
  // Pre-edit 3.2 s of equal taps: fills Smooth's 3 s window and lets
  // Fast's 15 ms follower settle; both sit at 0 dB.
  const std::size_t pre_n = seconds(3.2);
  const std::size_t post_n = seconds(0.6);
  const std::vector<float> pre_wet = sine_1k(0.0, pre_n);
  const std::vector<float> pre_dry = sine_1k(0.0, pre_n);
  const std::vector<float> post_wet = sine_1k(-6.0, post_n);  // wet edit
  const std::vector<float> post_dry = sine_1k(0.0, post_n);

  auto run = [&](dsp::GainComputer& g) {
    std::vector<float> tmp;
    feed(g, pre_wet, pre_dry, tmp, {479u});
    std::vector<float> out;
    feed(g, post_wet, post_dry, out, {479u});
    return out;
  };

  dsp::GainComputer g_fast(kRate);   // FR-28 default
  CHECK(g_fast.mode() == dsp::GainMode::fast);
  g_fast.set_ramp_tc(0.05);          // pin the 50 ms default this case assumes
  g_fast.set_engaged(true);
  const auto out_fast = run(g_fast);

  dsp::GainComputer g_smooth = smooth_engine();
  g_smooth.set_ramp_tc(0.05);
  g_smooth.set_engaged(true);
  const auto out_smooth = run(g_smooth);

  // 200 ms after the edit. Fast: detector done in ~5 tau (75 ms) and the
  // 50 ms ramp at 1-e^-4 ~ 98 % -> ~ +5.9 dB.
  const std::size_t at = seconds(0.2) - 1;
  CHECK(out_fast.at(at) > 5.0);
  CHECK(out_fast.at(at) < 6.1);
  // Smooth: the 3 s window still holds ~93 % of the old level, so its
  // target has barely moved.
  CHECK(out_smooth.at(at) < 1.0);
  // Fast is fully there by the end of the 600 ms window.
  CHECK(out_fast.back() == Approx(6.0).margin(0.05));
}

// ---------------------------------------------------------------------------
// R-G2 — Fast floor: corner = max(HP, 80), 80 with the HP off; Smooth
// has no floor. 40 Hz at 0 dBFS: an 80 Hz 4th-order corner costs the
// one-octave-below tone ~24 dB (the R-F6 number); 150 Hz costs ~46 dB.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: Fast detector floor is 80 Hz (R-G2)") {
  const std::size_t n = seconds(3.2);  // > the 3 s Smooth window
  const std::vector<float> tone = sine_at(40.0, 0.0, n);

  auto fast_reading = [&](bool hp, double hz) {
    dsp::GainComputer g(kRate);   // default Fast
    g.set_meter_hp(hp, hz);
    std::vector<float> out;
    feed(g, tone, tone, out, {479u, 13u});
    return g.wet_loudness_db();
  };

  const double hp_off = fast_reading(false, 150.0);
  const double hp_40 = fast_reading(true, 40.0);
  const double hp_150 = fast_reading(true, 150.0);

  // -3 dB mean square at 0 dBFS, minus ~24 dB one octave below 80.
  CHECK(hp_off == Approx(-27.1).margin(1.5));
  // The floor dominates a user corner below it: bit-identical stages.
  CHECK(hp_40 == Approx(hp_off).margin(1e-9));
  // Above the floor the user's corner IS the corner (~-46 dB at 40 Hz).
  CHECK(hp_150 == Approx(-48.9).margin(1.5));

  // Smooth keeps today's stage: HP off = no meter filter, and the
  // K-weighting high-pass sits at 38 Hz, so 40 Hz reads near the anchor.
  dsp::GainComputer g_smooth = smooth_engine();
  std::vector<float> out;
  feed(g_smooth, tone, tone, out, {479u, 13u});
  CHECK(g_smooth.wet_loudness_db() > -10.0);
  CHECK(g_smooth.wet_loudness_db() < 0.0);
}

// ---------------------------------------------------------------------------
// R-G3 — mode switch mid-run: both detectors are warm, so the target
// re-aims at the next evaluation and the ramp carries the change.
// Content: 1 kHz dry vs 6 kHz wet at equal amplitude — the K shelf
// separates Smooth's LUFS; Fast's broadband level reads them as equal.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: mode switch re-aims from the running detectors (R-G3)") {
  const std::size_t n = seconds(6.5);
  const auto dry = sine_at(1000.0, 0.0, n);
  const auto wet = sine_at(6000.0, 0.0, n);

  // Oracle for the Smooth side (no hand-rolled K-weighting math).
  dsp::LoudnessMeter m_wet(kRate, 3.0);
  dsp::LoudnessMeter m_dry(kRate, 3.0);
  m_wet.process(wet.data(), n);
  m_dry.process(dry.data(), n);
  const double delta_lufs = m_wet.snapshot().lufs - m_dry.snapshot().lufs;
  CHECK(delta_lufs > 0.5);

  dsp::GainComputer g = smooth_engine();
  g.set_engaged(true);
  std::vector<float> out;
  feed(g, wet, dry, out, {479u, 13u});
  CHECK(g.mode() == dsp::GainMode::smooth);
  CHECK(g.applied_gain_db() == Approx(-delta_lufs).margin(0.05));

  // Switch: no reset, no gap — the next evaluation re-aims at 0 dB.
  g.set_mode(dsp::GainMode::fast);
  CHECK(g.mode() == dsp::GainMode::fast);
  const double before = g.applied_gain_db();
  std::vector<float> after;
  feed(g, wet, dry, after, {479u, 13u});

  // Continuous: the first step is <= one one-pole step, never a slam.
  const double alpha = 1.0 - std::exp(-1.0 / (kRate * 0.05));
  CHECK(std::abs(after[0] - before) <= (std::abs(delta_lufs) + 1.0) * alpha);
  CHECK(std::abs(after.back()) < 0.05);  // re-aimed at the Fast delta ~ 0
}

// ---------------------------------------------------------------------------
// R-G4 — the silence hold and its flag are TIME-based (5 s), not
// slot-count-based: equal on the 5 ms Fast grid and the 100 ms Smooth
// grid.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: silence hold is time-based on both grids (R-G4)") {
  auto run = [](dsp::GainMode mode) {
    dsp::GainComputer g(kRate);
    g.set_mode(mode);
    g.set_engaged(true);

    // Settle to +6 dB (wet -6 dB); 4 s also fills Smooth's window.
    const std::size_t settle_n = seconds(4.0);
    std::vector<float> tmp;
    feed(g, sine_1k(-6.0, settle_n), sine_1k(0.0, settle_n), tmp,
         {479u, 13u});
    const double settled = g.applied_gain_db();
    CHECK(settled == Approx(6.0).margin(0.05));

    // 4.9 s of digital silence on the wet tap: held, flag still dark.
    const std::size_t gap_n = seconds(4.9);
    const std::vector<float> gap(gap_n, 0.0f);
    std::vector<float> out;
    feed(g, gap, sine_1k(0.0, gap_n), out, {479u, 13u});
    CHECK(g.applied_gain_db() == Approx(settled).margin(1e-6));
    CHECK(g.flags().no_signal_wet == false);  // 4.9 s < 5 s
    CHECK(g.flags().dry_tap_missing == false);  // dry never silent

    // A further 0.2 s crosses the 5 s line: the flag lights.
    const std::size_t tail_n = seconds(0.2);
    const std::vector<float> tail(tail_n, 0.0f);
    std::vector<float> out2;
    feed(g, tail, sine_1k(0.0, tail_n), out2, {479u});
    CHECK(g.flags().no_signal_wet == true);

    // Real wet audio returns -> the flag clears on the next slot.
    const std::size_t back_n = seconds(0.5);
    std::vector<float> out3;
    feed(g, sine_1k(-6.0, back_n), sine_1k(0.0, back_n), out3, {13u});
    CHECK(g.flags().no_signal_wet == false);
  };

  run(dsp::GainMode::fast);
  run(dsp::GainMode::smooth);
}

// ---------------------------------------------------------------------------
// R-G5 — chunk invariance on the 5 ms grid, plus its response time: the
// target reacts well inside 100 ms, so the gain has left zero 20 ms
// after the edit (a 100 ms slot could not).
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: Fast chunk invariance and 5 ms response (R-G5)") {
  const std::size_t pre_n = seconds(0.5);
  const std::size_t post_n = seconds(0.5);
  std::vector<float> wet = sine_1k(0.0, pre_n);
  { const auto tail = sine_1k(-6.0, post_n);
    wet.insert(wet.end(), tail.begin(), tail.end()); }
  const std::vector<float> dry = sine_1k(0.0, wet.size());

  dsp::GainComputer g_ragged(kRate);   // default Fast
  g_ragged.set_ramp_tc(0.05);          // pin the 50 ms value the bounds use
  g_ragged.set_engaged(true);
  std::vector<float> out_ragged;
  feed(g_ragged, wet, dry, out_ragged, {7u, 11u, 13u, 1u, 479u});

  dsp::GainComputer g_uniform(kRate);
  g_uniform.set_ramp_tc(0.05);
  g_uniform.set_engaged(true);
  std::vector<float> out_uniform;
  feed(g_uniform, wet, dry, out_uniform, {48000u});

  CHECK(out_ragged == out_uniform);

  // 20 ms after the edit: one 5 ms evaluation has fired and the 15 ms
  // follower is well on its way — the ramp has clearly moved.
  const std::size_t at = pre_n + seconds(0.02) - 1;
  CHECK(out_uniform.at(at) > 0.2);
  CHECK(out_uniform.back() == Approx(6.0).margin(0.05));
}

// ---------------------------------------------------------------------------
// R-G6 — the null invariant survives the Fast path: identical taps give
// an EXACTLY zero delta (the floor HP and the follower see the same
// samples on both taps).
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: identical taps stay exactly 0 dB in both modes (R-G6)") {
  const std::size_t n = seconds(3.2);  // keeps Smooth's window valid
  const std::vector<float> program = noise(n);

  for (const auto mode : {dsp::GainMode::fast, dsp::GainMode::smooth}) {
    dsp::GainComputer g(kRate);
    g.set_mode(mode);
    g.set_engaged(true);
    std::vector<float> out;
    feed(g, program, program, out, {7u, 1u, 479u, 13u});
    CHECK(g.delta_valid());
    CHECK(g.delta_db() == Approx(0.0).margin(1e-9));
    CHECK(std::abs(g.applied_gain_db()) < 0.01);
  }
}

// ---------------------------------------------------------------------------
// R-G7 — FR-30: the mode is locked while frozen. A refused switch leaves
// the mode and the held gain untouched; after release the switch works.
// ---------------------------------------------------------------------------
TEST_CASE("gain computer: mode change is refused while frozen (R-G7)") {
  dsp::GainComputer g = smooth_engine();
  g.set_engaged(true);
  std::vector<float> tmp;
  // Settle +6 (wet -6 dB) so there is a held value to protect.
  feed(g, sine_1k(-6.0, seconds(4.0)), sine_1k(0.0, seconds(4.0)), tmp,
       {479u, 13u});
  const double settled = g.applied_gain_db();
  CHECK(settled == Approx(6.0).margin(0.05));

  g.set_frozen(true);
  g.set_mode(dsp::GainMode::fast);   // refused
  CHECK(g.mode() == dsp::GainMode::smooth);
  CHECK(g.applied_gain_db() == Approx(settled).margin(1e-9));

  // Content changes under the freeze still do not move the gain.
  std::vector<float> out;
  feed(g, sine_1k(-12.0, seconds(0.5)), sine_1k(0.0, seconds(0.5)), out,
       {13u});
  CHECK(g.applied_gain_db() == Approx(settled).margin(1e-6));

  g.set_frozen(false);
  g.set_mode(dsp::GainMode::fast);   // now accepted
  CHECK(g.mode() == dsp::GainMode::fast);
}
