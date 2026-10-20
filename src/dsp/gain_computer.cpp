// src/dsp/gain_computer.cpp — the compensate machine (GREEN).
//
// Design that makes the whole FR-2..FR-11 behavior testable headless
// (the host-facing processor is thin glue, PR-B PB2):
//
//   * ONE rolling LoudnessMeter serves every role. The training "window
//     after the press" does not need a meter reset: the meter rolls
//     continuously, and the state AT press+3.0 s of a rolling 3.0 s
//     window IS the post-press window. Completion is counted internally
//     in 100 ms evaluation slots, exactly mirroring the meter's own
//     BS.1770 sub-block convention.
//   * Target evaluation happens ONLY on those 100 ms sample-position
//     boundaries and depends on nothing else — so the machine's
//     trajectory is a pure function of the fed stream, which is what
//     the R3 block-invariance tests pin (identical applied-gain trace
//     at fixed sample checkpoints regardless of host chunking).
//   * The applied gain ramps per sample with a one-pole on the current
//     target (cached α, one multiply per sample — allocation-free, no
//     locks; process() is audio-thread safe).
//   * Silence (snapshot LUFS at/under kSilenceLufs, whichever metric is
//     selected) freezes the chase: the target tracks the applied gain
//     so the ramp does not move (FR-9 / TS §9 ③). Metric selection only
//     picks the reference/live pairing; the silence gate stays on LUFS.
//
// Thread contract: ctor only at construction time; setters are plain
// member writes (cheap); process() is realtime-safe.

#include "gain_computer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace makegain::dsp {

namespace {

// Evaluation slot in seconds — mirrors the meter's 100 ms sub-block
// accumulation convention (BS.1770 metering). File-local on purpose: the
// invariance property needs AN internal fixed schedule, not the meter's.
constexpr double kEvalSlotSec = 0.1;

// 3.0 s training window expressed in evaluation slots.
constexpr std::size_t kTrainingSlots =
    static_cast<std::size_t>(GainComputer::kTrainingWindowSec / kEvalSlotSec);

// Encode a double's bit pattern into shift-assembled little-endian bytes.
// Shifts are endian-free on the machine; the messageByteBuffer order is
// therefore a structural, host-independent contract (codec purity, TS §9 ⑤).
void put_bits_le(std::vector<std::uint8_t>& out, std::uint64_t bits) {
  for (unsigned i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
  }
}

std::uint64_t get_bits_le(const std::uint8_t* p) {
  std::uint64_t bits = 0;
  for (unsigned i = 0; i < 8; ++i) {
    bits |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return bits;
}

// double <-> its bit pattern (memcpy is the portable bit-pattern cast).
std::uint64_t double_bits(double d) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof(bits));
  return bits;
}

double bits_double(std::uint64_t bits) {
  double d = 0.0;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}

}  // namespace

GainComputer::GainComputer(double sample_rate)
    : meter_(sample_rate, kTrainingWindowSec), sample_rate_(sample_rate) {
  // One-pole α per sample: applied = applied + (target−applied)·α.
  const double per_sample = 1.0 / sample_rate;
  const double tc = kDefaultRampTcSec;
  alpha_ = 1.0 - std::exp(-per_sample / tc);
  eval_slot_samples_ =
      static_cast<std::uint64_t>(sample_rate * kEvalSlotSec + 0.5);
}

void GainComputer::set_mode(GainMode mode) {
  // (Re)entering READ always starts a fresh run — "press training" means
  // exactly that (FR-2 first press, FR-4 re-press both behave alike).
  if (mode == GainMode::reading && !(mode_ == GainMode::reading &&
                                     run_slots_ == 0)) {
    run_slots_ = 0;  // old reference stays valid until completion (FR-4)
  }
  mode_ = mode;
}

void GainComputer::set_metric(GainMetric metric) {
  // FR-8: a pure member write — the next evaluation re-targets from the
  // SAME reference record and the SAME live window. No reset anywhere.
  metric_ = metric;
}

void GainComputer::set_max_comp_db(double db) { max_comp_db_ = db; }

void GainComputer::set_ramp_tc(double seconds) {
  if (seconds <= 0.0) return;  // nonsense input ignored, contract intact
  ramp_tc_ = seconds;
  alpha_ = 1.0 - std::exp(-(1.0 / sample_rate_) / ramp_tc_);
}

void GainComputer::process(const float* samples, std::size_t count) {
  // Stream the block in ≤ one evaluation-slot steps: feed the meter,
  // evaluate exactly when the slot boundary is crossed, ramp the same
  // samples toward whatever target is current. Host chunk sizes can
  // never change the trajectory (the R3 property).
  std::size_t i = 0;
  while (i < count) {
    const std::uint64_t room = eval_slot_samples_ - slot_fed_;
    const std::size_t take = static_cast<std::size_t>(
        std::min<std::uint64_t>(room, count - i));
    meter_.process(samples + i, take);  // realtime-safe accumulation
    advance_ramp(take);                 // per-sample one-pole below
    slot_sq_ += slot_energy(samples + i, take);
    i += take;
    slot_fed_ += take;
    if (slot_fed_ == eval_slot_samples_) {
      slot_fed_ = 0;
      // Wall-side silence verdict (FR-9): the gate is on the RAW newest
      // slot, not the rolling window — a window morphs for seconds after
      // a mute and reads like a real decline, which FR-3 then chases.
      // Digital silence at the wall is unambiguous → freeze instantly.
      tail_silent_ = (slot_sq_ / static_cast<double>(eval_slot_samples_) <=
                      kSilenceMeanSq);
      silent_streak_ = tail_silent_ ? silent_streak_ + 1 : 0;
      flags_.no_signal = (silent_streak_ >= kNoSignalSlots);  // 5.0 s
      slot_sq_ = 0.0;
      evaluate();
    }
  }
}

void GainComputer::evaluate() {
  const LoudnessSnapshot s = meter_.snapshot();

  if (mode_ == GainMode::reading) {
    ++run_slots_;
    if (run_slots_ >= kTrainingSlots && s.valid) {
      // FR-2: the run completes ON ITS OWN; FR-6a: BOTH metrics from
      // the same window; FR-5a: silence is a measured reference that
      // flags low confidence. Completion always returns to IDLE
      // (FR-2: un-engaged until the user engages Adjust). This REPLACES
      // any previous reference (FR-4) — never mid-run.
      // (s.valid cannot be false at slot 30 since the B5 meter fix —
      // the boundary-exact window is then fully accumulated — but the
      // gate keeps the FR-2 contract airtight for late-capture corners.)
      reference_.lufs = s.lufs;
      reference_.db_rms = s.db_rms;
      reference_.valid = true;
      flags_.low_confidence = (s.lufs <= kSilenceLufs);
      flags_.clamped = false;
      mode_ = GainMode::idle;
      target_db_ = applied_db_;  // idle: ramp home to 0 dB from here
    }
    // While the run is live: passthrough with the ramp heading to 0 —
    // no compensation while reading (the reading target stays 0 dB).
    if (mode_ == GainMode::reading) {
      target_db_ = 0.0;
    }
    return;
  }

  if (mode_ == GainMode::adjusting) {
    if (tail_silent_ || !s.valid || s.lufs <= kSilenceLufs) {
      // FR-9 / TS §9 ③: silence at the wall (or a not-yet-full window)
      // → STOP chasing: the target dissolves into the applied gain, so
      // the ramp holds exactly where it is (no drift to zero, no
      // runaway). The gate is wall-side on purpose: by the time the
      // rolling window itself reads silence, seconds of mixed values
      // would already have been chased (observed as applied +10 dB in
      // the first green run — see the R4 comment in the spec file).
      target_db_ = applied_db_;
    } else if (reference_.valid) {
      // FR-3: g = ref[metric] − live[metric], FR-10 clamp on top, FR-8
      // free re-pairing via metric_ (the record carries both metrics).
      const double live =
          (metric_ == GainMetric::lufs ? s.lufs : s.db_rms);
      const double rec =
          (metric_ == GainMetric::lufs ? reference_.lufs
                                       : reference_.db_rms);
      const double raw = rec - live;  // −inf reference clamps below (R6b)
      flags_.clamped = std::fabs(raw) > max_comp_db_;  // truthful flag
      target_db_ = std::max(-max_comp_db_,
                            std::min(max_comp_db_, raw));
    } else {
      target_db_ = applied_db_;  // engage before any READ ever: hold 0
    }
    return;
  }

  // IDLE: the ramp always heads home to 0 dB (FR-5 engage semantics in
  // both directions — a decaying approach, never a hard slam).
  target_db_ = 0.0;
}

void GainComputer::advance_ramp(std::size_t count) {
  for (std::size_t j = 0; j < count; ++j) {
    applied_db_ += (target_db_ - applied_db_) * alpha_;
  }
}

// Raw (unweighted) mean-square energy of a mono chunk — only used for
// the wall-side silence verdict (K-weighting is irrelevant there).
double GainComputer::slot_energy(const float* samples, std::size_t count) {
  double sum = 0.0;
  for (std::size_t j = 0; j < count; ++j) {
    const double x = static_cast<double>(samples[j]);
    sum += x * x;
  }
  return sum;
}

GainMode GainComputer::mode() const { return mode_; }

GainMetric GainComputer::metric() const { return metric_; }

ReferenceRecord GainComputer::reference() const { return reference_; }

GainFlags GainComputer::flags() const { return flags_; }

double GainComputer::applied_gain_db() const { return applied_db_; }

std::vector<std::uint8_t> GainComputer::encode_reference() const {
  if (!reference_.valid) return {};  // no valid record → no bytes
  std::vector<std::uint8_t> out;
  out.reserve(18);
  out.push_back(1);  // version tag (first byte of record)
  out.push_back(0);  // reserved flags byte 0: parse-strict in decode
  put_bits_le(out, double_bits(reference_.lufs));
  put_bits_le(out, double_bits(reference_.db_rms));
  return out;
}

bool GainComputer::decode_reference(const std::uint8_t* data,
                                    std::size_t size) {
  // All-or-nothing: nothing is written before every check passes.
  if (size < 18 || data[0] != 1 || data[1] != 0) return false;
  // Trailing bytes beyond the 18 this version knows are IGNORED, not
  // fatal (forward compatibility — a future version may append fields).
  const double lufs = bits_double(get_bits_le(data + 2));
  const double db_rms = bits_double(get_bits_le(data + 10));
  reference_.lufs = lufs;
  reference_.db_rms = db_rms;
  reference_.valid = true;
  return true;
}

}  // namespace makegain::dsp
