// tests/dsp/gain_computer_test.cpp — the compensate machine spec (RED
// phase; AGENTS.md rule 8: the owner reviewed these assertions before any
// real implementation exists — owner review 2026-10-20 applied: trace-
// based block invariance (R3), transparent metric-switch assertion (R8),
// clamp transition still ramps (R2), silence sentinel pinned (R6b),
// tautological step bound dropped from R5 in favor of R3, structural
// codec-byte audit (R7), fixed-granularity feed helper).
//
// Case map (R# refs the PR-B plan in doc/ai/m1-plan.md; FR# the frozen
// contract doc/human/MakeGain_FR-v1.md):
//
//   R6  READ self-completes exactly at the 3.0 s code window (FR-2/FR-4),
//       captures BOTH metrics from the same window (FR-6a), and leaves
//       the machine IDLE.
//   R6b READ over silence still completes; the record holds the silence
//       sentinel (−inf, not 0 dB) and low_confidence TRUE (FR-5a). The
//       flag is a property of the current reference: it persists after
//       the reference is put to work; a fresh READ completion clears it.
//       Engaging a silent record: target = −inf − live clamps to −24 dB.
//   R5  Engage from IDLE starts proving at ~0 dB (no slam, no re-train,
//       FR-5). Identity property folded in: engaging against the same
//       program it was trained on ≈ 0 dB compensation (FR-3 no-op).
//       Zipper-freeness is NOT asserted here by a step bound (a one-pole
//       trivially satisfies any such bound — tautological); it is proven
//       per-sample by R3's trace equality.
//   R3  Ramp = per-sample one-pole → BLOCK INVARIANCE: the same stream
//       fed in different chunk sizes produces an identical applied-gain
//       TRACE at fixed 480-sample checkpoints (10 ms) across the whole
//       run, not merely the same endpoint. This is the property that
//       makes ragged hosts harmless and automation safe.
//   R1  Compensation settles at ref[metric] − live[metric] (FR-3).
//   R8  Metric switch (FR-8): re-targets WITHOUT re-read — reference
//       untouched, and the switch is transparent at the anchor tone
//       (both pairings coincide there, the PR-A B4 cross-tie): same
//       settled −6.02 dB, no jump at the switch moment. A metric-spread
//       re-target proof needs a non-anchor program with a pinned K-gain;
//       deferred until one is golden-verified.
//   R2  Clamp (FR-10): |target| beyond MaxComp dB applies AT the bound;
//       clamped flag truthful, both signs — AND the clamp successor
//       still ramps: per-call applied gain moves < 0.1 dB, so no
//       implementation may jump straight to ±24.
//   R4  Silence-hold (FR-9 / TS §9 ③): silence freezes the last applied
//       gain (no chase-to-zero, no runaway); a valid signal resumes.
//   R7  Reference codec (FR-7): round-trip identity; behavioral (the
//       decoded machine compensates identically); trailing bytes ignored
//       (forward compat); garbage refused WITHOUT touching the stored
//       reference, truncated header refused; byte-structure audited
//       (18 bytes, version tag first). NOTE: the round-trip itself is a
//       same-host test; the cross-endian guarantee is structural —
//       shift-assembled little-endian (header contract), not executed.
//
// Golden math (PR-A B1/B2 ground truth runs through every number here):
//   The 997 Hz/48 kHz K-chain answers +0.691 dB (≈ flat), so for a
//   full-scale-amp/a sine:
//     lufs   = −3.009 + 20·log10(a)
//     db_rms = −3.010 + 20·log10(a)     (both −inf when a = 0)
//   amp 0.5 → pair ≈ −9.03 dB; amp 1.0 → ≈ −3.01 dB; amp 0.05 → −29.03.
// Method: machines are driven with ragged block feeds (7/11/13 cycling)
// wherever possible, then settled well past window-freshness (3.0 s) and
// ramp convergence (≈ 5·tc ≈ 1.5 s) before the settled-gain assertions.

#include "gain_computer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

using makegain::dsp::GainComputer;
using makegain::dsp::GainMetric;
using makegain::dsp::GainMode;

namespace {

constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kRate = 48000.0;

// Deterministic 997 Hz sine of `seconds`, amplitude `amp`, fresh phase.
std::vector<float> sine(double seconds, double amp) {
  const std::size_t n = static_cast<std::size_t>(seconds * kRate);
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; ++i) {
    v[i] = static_cast<float>(
        amp * std::sin(2.0 * kPi * 997.0 * static_cast<double>(i) / kRate));
  }
  return v;
}

std::vector<float> zeros(double seconds) {
  return std::vector<float>(static_cast<std::size_t>(seconds * kRate), 0.0f);
}

// Feed the whole buffer in cycling ragged chunks of 7/11/13 samples —
// the worst-case accumulation pattern that also hosts really use.
void feed_ragged(GainComputer& g, const std::vector<float>& v) {
  static const std::size_t chunks[] = {7, 11, 13};
  std::size_t i = 0;
  std::size_t k = 0;
  while (i < v.size()) {
    const std::size_t take = std::min(chunks[k % 3], v.size() - i);
    g.process(v.data() + i, take);
    i += take;
    ++k;
  }
}

// Ragged feed that also tracks the max per-CALL applied-gain move (used
// to prove a transition still ramps — a direct jump to a target, e.g. a
// clamped value applied without the ramp, shows up as a multi-dB step).
double feed_ragged_tracked(GainComputer& g, const std::vector<float>& v) {
  static const std::size_t chunks[] = {7, 11, 13};
  double prev = g.applied_gain_db();
  double max_step = 0.0;
  std::size_t i = 0;
  std::size_t k = 0;
  while (i < v.size()) {
    const std::size_t take = std::min(chunks[k % 3], v.size() - i);
    g.process(v.data() + i, take);
    i += take;
    ++k;
    const double now = g.applied_gain_db();
    max_step = std::max(max_step, std::fabs(now - prev));
    prev = now;
  }
  return max_step;
}

// Feed `n` samples at a fixed max granularity (`gran` = worst chunk) and
// record the applied gain at every `trace_every`-sample boundary. The
// boundary-crossing aligns the trace to absolute sample positions no
// matter the chunking — large `gran` chunks are simply split at their
// next boundary, so traces from different granularities are element-wise
// comparable (same times).
void feed_fixed_traced(GainComputer& g, const float* v, std::size_t n,
                       std::size_t gran, std::size_t trace_every,
                       std::vector<double>& trace) {
  std::size_t pos = 0;
  std::size_t next = trace_every;
  while (pos < n) {
    const std::size_t lim = std::min(n, next);
    const std::size_t take = std::min(gran, lim - pos);
    g.process(v + pos, take);
    pos += take;
    if (pos == next) {
      trace.push_back(g.applied_gain_db());
      next += trace_every;
    }
  }
}

// Full-trajectory comparison for R3: traces must agree at every aligned
// checkpoint, not just at the settle point — a buggy accumulator that
// drifts mid-stream and converges to the same fixed point FAILS here.
void check_traces_equal(const std::vector<double>& a,
                        const std::vector<double>& b) {
  REQUIRE(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::fabs(a[i] - b[i]) >
        1e-9 * std::max(1.0, std::fabs(a[i]))) {
      FAIL("applied-gain traces diverge at checkpoint " << i << ": "
           << a[i] << " vs " << b[i]);
      return;
    }
  }
}

// Run a full READ on `program` and assert FR-2/FR-4 completion semantics.
void train(GainComputer& g, const std::vector<float>& program) {
  g.set_mode(GainMode::reading);
  feed_ragged(g, program);
  REQUIRE(g.mode() == GainMode::idle);  // self-completing, FR-2
  REQUIRE(g.reference().valid);         // both metrics captured, FR-6a
}

}  // namespace

TEST_CASE("gain computer: READ self-completes at the 3.0 s window (R6)") {
  GainComputer g(kRate);

  // Just BEFORE the window: no reference yet. Note on probe tightness
  // (owner review): the exact 3.0 s boundary is unreachable under the
  // ragged 7/11/13 feeds — completion MUST count samples (slot-aligned
  // counters), which is precisely what B5 pins on the meter side.
  g.set_mode(GainMode::reading);
  feed_ragged(g, sine(2.9, 0.5));
  CHECK_FALSE(g.reference().valid);

  // The remaining 0.3 s crosses 3.0 s → the run completes ON ITS OWN
  // (no second press) and captures BOTH metrics of amp 0.5 content:
  // lufs ≈ −9.030, db_rms ≈ −9.031 (see golden math in the header).
  feed_ragged(g, sine(0.3, 0.5));
  REQUIRE(g.reference().valid);
  CHECK(g.reference().lufs == Catch::Approx(-9.030).margin(0.06));
  CHECK(g.reference().db_rms == Catch::Approx(-9.031).margin(0.06));
  CHECK(g.mode() == GainMode::idle);  // FR-2: un-engaged until Adjust
  CHECK_FALSE(g.flags().low_confidence);
}

TEST_CASE(
    "gain computer: READ over silence completes, sentinel −inf, flag "
    "persists into use (R6b / FR-5a)") {
  GainComputer g(kRate);
  train(g, zeros(3.2));
  REQUIRE(g.reference().valid);
  // Silence in the window is a MEASURED reference — the B-metric
  // contract says silence reads −inf: pinned here, never silently 0 dB.
  CHECK(!std::isfinite(g.reference().lufs));
  CHECK(!std::isfinite(g.reference().db_rms));
  CHECK(g.flags().low_confidence);

  // The flag is a property of the current reference: it persists after
  // the reference is put to work and clears only on a new READ. Engaging
  // the silent record: target = −inf − live → beyond any clamp → −24 dB.
  g.set_mode(GainMode::adjusting);
  feed_ragged(g, sine(8.0, 1.0));
  CHECK(g.flags().low_confidence);
  CHECK(g.applied_gain_db() == Catch::Approx(-24.0).margin(0.06));

  // A fresh successful READ clears the flag (record replaced).
  g.set_mode(GainMode::reading);
  feed_ragged(g, sine(3.2, 1.0));
  CHECK_FALSE(g.flags().low_confidence);
}

TEST_CASE("gain computer: engage from IDLE proves at ~0 dB (R5)") {
  GainComputer g(kRate);
  train(g, sine(3.2, 0.5));  // reference = amp 0.5 program

  g.set_mode(GainMode::adjusting);
  // FR-9/③ hold made precondition-explicit (owner review, 2026-10-22):
  // feed a moment of silence FIRST so "no move" is asserted on a
  // machine state that provably has a stale/no window — not on
  // implementation-dependent initialization order.
  feed_ragged(g, zeros(0.1));
  CHECK(std::fabs(g.applied_gain_db()) <= 0.05);

  // Identity property (FR-3 no-op): feeding the SAME program the
  // reference was trained on settles the compensation at ≈ 0 dB,
  // approached smoothly from 0. Zipper-freeness is proven per-sample by
  // R3's trace equality; a step bound here would be a tautology.
  feed_ragged(g, sine(8.0, 0.5));
  CHECK(g.applied_gain_db() == Catch::Approx(0.0).margin(0.06));
}

TEST_CASE(
    "gain computer: block invariance — identical applied-gain TRACES at "
    "every 480-sample checkpoint, all chunkings (R3)") {
  const std::vector<float> train_prog = sine(3.2, 0.5);
  const std::vector<float> adj_prog = sine(6.0, 1.0);
  // 320 checkpoints across READ + 600 across ADJUST (every 10 ms).
  constexpr std::size_t kExpectedTrace = (153600 + 288000) / 480;

  auto run = [&](std::size_t gran) {
    GainComputer g(kRate);
    std::vector<double> trace;
    g.set_mode(GainMode::reading);
    feed_fixed_traced(g, train_prog.data(), train_prog.size(), gran, 480,
                      trace);
    g.set_mode(GainMode::adjusting);
    feed_fixed_traced(g, adj_prog.data(), adj_prog.size(), gran, 480, trace);
    // Trace LENGTH may sit within one checkpoint of the arithmetic
    // window (completion at ==3.0 s vs first-post-boundary evaluation —
    // an off-by-one here is semantics timing, not a bug; the CONTENT
    // comparison below is exact regardless).
    REQUIRE(trace.size() >= kExpectedTrace - 1);
    REQUIRE(trace.size() <= kExpectedTrace + 1);
    return trace;
  };

  // Endpoint first (the R1 settled golden), then the FULL trajectories:
  // the `1`, `7` (worst ragged) and `48000` (one call per second) traces
  // must match the `480` reference trace ELEMENT-WISE over the whole run.
  const auto base = run(480);
  CHECK(base.back() == Catch::Approx(-6.02).margin(0.06));
  check_traces_equal(run(1), base);
  check_traces_equal(run(7), base);
  check_traces_equal(run(48000), base);
}

TEST_CASE("gain computer: settling at ref − live; metric switch transparent "
          "at the anchor (R1; R8 partial)") {
  GainComputer g(kRate);
  train(g, sine(3.2, 0.5));  // ref pair ≈ −9.03 both metrics

  // Chain got 6.02 dB louder (amp 0.5 → 1.0): compensation settles at
  // ref − live ≈ −6.02 dB of attenuation.
  g.set_mode(GainMode::adjusting);
  feed_ragged(g, sine(8.0, 1.0));  // > window + 5·tc: fully settled
  CHECK(g.applied_gain_db() == Catch::Approx(-6.02).margin(0.06));
  CHECK_FALSE(g.flags().clamped);

  // FR-8: switching metric re-targets instantly from the SAME record —
  // the reference is untouched by the switch.
  const auto ref_before = g.reference();
  const double pre_switch = g.applied_gain_db();
  g.set_metric(GainMetric::db_rms);
  CHECK(g.reference().lufs == ref_before.lufs);  // untouched, FR-8
  CHECK(g.reference().db_rms == ref_before.db_rms);
  CHECK(g.reference().valid);

  // Transparency at the anchor tone: both pairings coincide there (the
  // PR-A B4 cross-tie), so the settled gain is the SAME value and the
  // switch moment is invisible — no jump, no re-read, no reset.
  feed_ragged(g, sine(2.0, 1.0));
  CHECK(g.applied_gain_db() == Catch::Approx(-6.02).margin(0.06));
  CHECK(std::fabs(g.applied_gain_db() - pre_switch) <= 0.06);
  CHECK_FALSE(g.flags().clamped);
}

TEST_CASE("gain computer: clamp at ±MaxComp with flag, both signs (R2)") {
  // Deafen the training (~−29.03 dB ref), then the chain runs 26 dB
  // louder → target ≈ −26 dB is beyond the +/− 24 bound: clamps to −24.
  GainComputer g(kRate);
  train(g, sine(3.2, 0.05));
  g.set_mode(GainMode::adjusting);
  const double step_lo = feed_ragged_tracked(g, sine(8.0, 1.0));
  CHECK(g.flags().clamped);
  CHECK(g.applied_gain_db() == Catch::Approx(-24.0).margin(0.06));
  // The clamp successor still RAMPS: a direct jump to the bound would
  // show as a ~24 dB move inside one ≤13-sample call (legit one-pole
  // tops out ≈ 0.024 dB here). Zipper-freeness per stage lives in R3.
  CHECK(step_lo < 0.1);

  // Opposite sign: reference 24.98 dB LOUDER than the live chain →
  // boost target ≈ +26 dB clamps at +24, still flagged, still ramped.
  GainComputer g2(kRate);
  train(g2, sine(3.2, 1.0));
  g2.set_mode(GainMode::adjusting);
  const double step_hi = feed_ragged_tracked(g2, sine(8.0, 0.02));
  CHECK(g2.flags().clamped);
  CHECK(g2.applied_gain_db() == Catch::Approx(24.0).margin(0.06));
  CHECK(step_hi < 0.1);
}

TEST_CASE("gain computer: silence freezes the applied gain, then recovers "
          "(R4 / FR-9)") {
  GainComputer g(kRate);
  train(g, sine(3.2, 0.5));
  g.set_mode(GainMode::adjusting);
  feed_ragged(g, sine(8.0, 1.0));
  REQUIRE(g.applied_gain_db() == Catch::Approx(-6.02).margin(0.06));

  // 8 s of digital silence: the machine does NOT chase to zero, does NOT
  // run away — the applied gain freezes at the last chasing value.
  feed_ragged(g, zeros(8.0));
  CHECK(g.applied_gain_db() == Catch::Approx(-6.02).margin(0.06));

  // Valid signal returns → the chase resumes to the same target.
  feed_ragged(g, sine(8.0, 1.0));
  CHECK(g.applied_gain_db() == Catch::Approx(-6.02).margin(0.06));
}

TEST_CASE("gain computer: reference codec round-trip + forward compat (R7)") {
  GainComputer g(kRate);
  train(g, sine(3.2, 0.5));
  const auto ref = g.reference();
  const auto bytes = g.encode_reference();
  REQUIRE(!bytes.empty());

  // Byte-structure audit (same-host round-trip; the cross-endian
  // guarantee is structural — shift-assembled little-endian, per the
  // header contract — not executed across hosts). The version tag is
  // referenced by constant, not literal (owner review): a future bump
  // breaks the test linkage, never the decoding of old sessions.
  REQUIRE(bytes.size() == 18);
  CHECK(bytes[0] == GainComputer::kCodecVersion);

  // Identity round-trip on a fresh machine.
  GainComputer g2(kRate);
  REQUIRE(g2.decode_reference(bytes.data(), bytes.size()));
  CHECK(g2.reference().lufs == ref.lufs);
  CHECK(g2.reference().db_rms == ref.db_rms);
  CHECK(g2.reference().valid);

  // Behavioral round-trip: the decoded machine compensates identically.
  g2.set_mode(GainMode::adjusting);
  feed_ragged(g2, sine(8.0, 1.0));
  CHECK(g2.applied_gain_db() == Catch::Approx(-6.02).margin(0.06));

  // Forward compatibility: trailing bytes nobody knows are ignored.
  auto with_tail = bytes;
  with_tail.push_back(0xAB);
  with_tail.push_back(0xCD);
  GainComputer g3(kRate);
  CHECK(g3.decode_reference(with_tail.data(), with_tail.size()));
  CHECK(g3.reference().valid);

  // Garbage is refused all-or-nothing: the already-valid reference of g3
  // must be untouched by a failed decode. The junk pattern leads with
  // 0x00 — version 0 is reserved-never-legal by the header contract, so
  // this stays invalid across every future version bump (owner review:
  // 0xEE-only junk would rot the day version 0xEE appears).
  std::vector<std::uint8_t> junk(64);
  for (std::size_t j = 0; j < junk.size(); ++j) {
    junk[j] = (j % 2 == 0) ? 0x00 : 0xFF;
  }
  const auto before = g3.reference();
  CHECK_FALSE(g3.decode_reference(junk.data(), junk.size()));
  CHECK(g3.reference().lufs == before.lufs);
  CHECK(g3.reference().db_rms == before.db_rms);

  // Truncated header: refused.
  CHECK_FALSE(g3.decode_reference(bytes.data(), 4));
}

TEST_CASE("gain computer: mid-READ transitions — abort, timer restart, "
          "replace-at-completion (FR-2/FR-4/FR-5)") {
  // Case A: engaging ADJUST mid-run ABORTS the run (header contract):
  // no phantom capture from the half window, and with no prior
  // reference the machine holds 0 dB — it cannot chase content it
  // never captured.
  GainComputer g(kRate);
  g.set_mode(GainMode::reading);
  feed_ragged(g, sine(1.0, 0.5));           // 1.0 s into the 3.0 s window
  g.set_mode(GainMode::adjusting);          // engage → abort (FR-5)
  feed_ragged(g, sine(8.0, 0.5));
  CHECK_FALSE(g.reference().valid);         // no phantom completion
  CHECK(g.mode() == GainMode::adjusting);
  CHECK(g.applied_gain_db() == Catch::Approx(0.0).margin(0.06));

  // Case B: pressing READ again mid-run RESTARTS the window (FR-2/FR-4:
  // a press is a press, its schedule resets). 3.0 s after the FIRST
  // press must still NOT complete; the edge moved with the re-press.
  g.set_mode(GainMode::reading);
  feed_ragged(g, sine(1.0, 0.5));
  g.set_mode(GainMode::reading);            // re-press: timer restarts NOW
  feed_ragged(g, sine(2.0, 0.5));           // 3.0 s since the FIRST press
  CHECK_FALSE(g.reference().valid);         // restart pushed the edge out
  feed_ragged(g, sine(3.2, 0.5));           // full 3.0 s on the 2nd press
  REQUIRE(g.reference().valid);
  CHECK(g.reference().lufs == Catch::Approx(-9.030).margin(0.06));

  // Case C: a re-read replaces the record ONLY at completion — junk
  // content fed to an aborted run leaves NO trace in the record.
  GainComputer g2(kRate);
  g2.set_mode(GainMode::reading);
  feed_ragged(g2, sine(3.2, 0.5));          // trained on amp 0.5
  g2.set_mode(GainMode::reading);           // re-read pressed
  feed_ragged(g2, sine(2.0, 1.0));          // "wrong program" junk fed in
  g2.set_mode(GainMode::adjusting);         // abort — junk never completes
  g2.set_mode(GainMode::reading);           // re-read again: fresh window
  feed_ragged(g2, sine(3.2, 0.5));          // completes on the clean program
  REQUIRE(g2.reference().valid);
  CHECK(g2.reference().lufs == Catch::Approx(-9.030).margin(0.06));
  CHECK(g2.reference().db_rms == Catch::Approx(-9.031).margin(0.06));
}
