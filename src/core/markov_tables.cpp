// Markov transition table scoring and model definitions.

#include "core/markov_tables.h"

#include <cmath>

#include "core/scale.h"

namespace bach {

// ---------------------------------------------------------------------------
// computeDegreeStep
// ---------------------------------------------------------------------------

DegreeStep computeDegreeStep(uint8_t from_pitch, uint8_t to_pitch,
                              Key key, ScaleType scale) {
  int from_deg = scale_util::pitchToAbsoluteDegree(from_pitch, key, scale);
  int to_deg = scale_util::pitchToAbsoluteDegree(to_pitch, key, scale);
  int step = to_deg - from_deg;
  if (step > 8) return 9;    // LargeLeapUp
  if (step < -8) return -9;  // LargeLeapDown
  return static_cast<DegreeStep>(step);
}

// ---------------------------------------------------------------------------
// Scoring functions
// ---------------------------------------------------------------------------

float scoreMarkovPitch(const MarkovModel& model,
                       DegreeStep prev_step, DegreeClass deg_class,
                       BeatPos beat, DegreeStep next_step) {
  int prev_idx = degreeStepToIndex(prev_step);
  int deg_cls = static_cast<int>(deg_class);
  int beat_pos = static_cast<int>(beat);
  int next_idx = degreeStepToIndex(next_step);

  int row = prev_idx * kDegreeClassCount * kBeatPosCount
          + deg_cls * kBeatPosCount
          + beat_pos;
  uint16_t prob_raw = model.pitch.prob[row][next_idx];

  // Compute row sum for normalization (should be ~10000 but verify).
  uint32_t row_sum = 0;
  for (int col = 0; col < kDegreeStepCount; ++col) {
    row_sum += model.pitch.prob[row][col];
  }
  if (row_sum == 0) return 0.0f;

  float prob = static_cast<float>(prob_raw) / static_cast<float>(row_sum);
  float p_uniform = 1.0f / static_cast<float>(kDegreeStepCount);

  // Avoid log(0).
  if (prob < 1e-7f) prob = 1e-7f;

  float raw = std::log(prob) - std::log(p_uniform);
  // Soft clip: tanhf(raw * 0.5) gives range ~[-0.46, +0.46].
  return std::tanh(raw * 0.5f);
}

float scoreMarkovDuration(const MarkovModel& model,
                          DurCategory prev_dur, DirIntervalClass dir_class,
                          DurCategory next_dur) {
  int prev_d = static_cast<int>(prev_dur);
  int dir_ivl = static_cast<int>(dir_class);
  int next_d = static_cast<int>(next_dur);

  int row = prev_d * kDirIvlClassCount + dir_ivl;
  uint16_t prob_raw = model.duration.prob[row][next_d];

  uint32_t row_sum = 0;
  for (int col = 0; col < kDurCatCount; ++col) {
    row_sum += model.duration.prob[row][col];
  }
  if (row_sum == 0) return 0.0f;

  float prob = static_cast<float>(prob_raw) / static_cast<float>(row_sum);
  float p_uniform = 1.0f / static_cast<float>(kDurCatCount);

  if (prob < 1e-7f) prob = 1e-7f;

  float raw = std::log(prob) - std::log(p_uniform);
  return std::tanh(raw * 0.5f);
}

// ---------------------------------------------------------------------------
// scoreVerticalInterval
// ---------------------------------------------------------------------------

float scoreVerticalInterval(const VerticalIntervalTable& table,
                            int bass_degree, BeatPos beat,
                            int voice_bin, HarmFunc hf, int pc_offset) {
  bass_degree = ((bass_degree % 7) + 7) % 7;
  pc_offset = ((pc_offset % 12) + 12) % 12;
  if (voice_bin < 0) voice_bin = 0;
  if (voice_bin > 2) voice_bin = 2;

  int row = verticalRowIndex(bass_degree, beat, voice_bin, hf);
  uint16_t prob_raw = table.prob[row][pc_offset];

  uint32_t row_sum = 0;
  for (int col = 0; col < kPcOffsetCount; ++col) {
    row_sum += table.prob[row][col];
  }
  if (row_sum == 0) return 0.0f;

  float prob = static_cast<float>(prob_raw) / static_cast<float>(row_sum);
  float p_uniform = 1.0f / static_cast<float>(kPcOffsetCount);

  if (prob < 1e-7f) prob = 1e-7f;

  float raw = std::log(prob) - std::log(p_uniform);
  return std::tanh(raw * 0.5f);
}

// ---------------------------------------------------------------------------
// getTopMelodicCandidates
// ---------------------------------------------------------------------------

int getTopMelodicCandidates(
    const MarkovModel& model,
    DegreeStep prev_step, DegreeClass deg_class, BeatPos beat,
    uint8_t from_pitch, Key key, ScaleType scale,
    uint8_t range_lo, uint8_t range_hi,
    OracleCandidate* out, int max_count) {
  int prev_idx = degreeStepToIndex(prev_step);
  int deg_cls = static_cast<int>(deg_class);
  int beat_pos = static_cast<int>(beat);

  int row = prev_idx * kDegreeClassCount * kBeatPosCount
          + deg_cls * kBeatPosCount
          + beat_pos;

  // Read row and compute total.
  uint32_t row_sum = 0;
  for (int col = 0; col < kDegreeStepCount; ++col) {
    row_sum += model.pitch.prob[row][col];
  }
  if (row_sum == 0) return 0;

  // Compute from_degree for pitch conversion.
  int from_deg = scale_util::pitchToAbsoluteDegree(from_pitch, key, scale);

  // Collect candidates with probabilities.
  struct RawCand {
    uint8_t pitch;
    float prob;
  };
  RawCand raw[kDegreeStepCount];
  int raw_count = 0;

  for (int col = 0; col < kDegreeStepCount; ++col) {
    DegreeStep step = static_cast<DegreeStep>(col - kDegreeOffset);
    int target_deg = from_deg + static_cast<int>(step);
    if (target_deg < 0) continue;

    uint8_t target_pitch = scale_util::absoluteDegreeToPitch(target_deg, key, scale);
    if (target_pitch < range_lo || target_pitch > range_hi) continue;

    float prob = static_cast<float>(model.pitch.prob[row][col])
               / static_cast<float>(row_sum);
    raw[raw_count++] = {target_pitch, prob};
  }

  // Sort by probability descending.
  for (int idx = 0; idx < raw_count - 1; ++idx) {
    for (int jdx = idx + 1; jdx < raw_count; ++jdx) {
      if (raw[jdx].prob > raw[idx].prob) {
        RawCand tmp = raw[idx];
        raw[idx] = raw[jdx];
        raw[jdx] = tmp;
      }
    }
  }

  // Output top-N.
  int count = (raw_count < max_count) ? raw_count : max_count;
  for (int idx = 0; idx < count; ++idx) {
    out[idx] = {raw[idx].pitch, raw[idx].prob};
  }
  return count;
}

// ---------------------------------------------------------------------------
// getTopVerticalCandidates
// ---------------------------------------------------------------------------

int getTopVerticalCandidates(
    const VerticalIntervalTable& table,
    int bass_degree, BeatPos beat, int voice_bin, HarmFunc hf,
    OracleCandidate* out, int max_count) {
  bass_degree = ((bass_degree % 7) + 7) % 7;
  if (voice_bin < 0) voice_bin = 0;
  if (voice_bin > 2) voice_bin = 2;

  int row = verticalRowIndex(bass_degree, beat, voice_bin, hf);

  uint32_t row_sum = 0;
  for (int col = 0; col < kPcOffsetCount; ++col) {
    row_sum += table.prob[row][col];
  }
  if (row_sum == 0) return 0;

  // Collect all 12 pitch classes with probabilities.
  struct RawCand {
    uint8_t pc;
    float prob;
  };
  RawCand raw[kPcOffsetCount];
  for (int col = 0; col < kPcOffsetCount; ++col) {
    raw[col] = {static_cast<uint8_t>(col),
                static_cast<float>(table.prob[row][col])
                    / static_cast<float>(row_sum)};
  }

  // Sort by probability descending.
  for (int idx = 0; idx < kPcOffsetCount - 1; ++idx) {
    for (int jdx = idx + 1; jdx < kPcOffsetCount; ++jdx) {
      if (raw[jdx].prob > raw[idx].prob) {
        RawCand tmp = raw[idx];
        raw[idx] = raw[jdx];
        raw[jdx] = tmp;
      }
    }
  }

  // Output top-N.
  int count = (kPcOffsetCount < max_count) ? kPcOffsetCount : max_count;
  for (int idx = 0; idx < count; ++idx) {
    out[idx] = {raw[idx].pc, raw[idx].prob};
  }
  return count;
}

// ---------------------------------------------------------------------------
// Auto-generated transition tables from Bach reference corpus
// ---------------------------------------------------------------------------

#include "core/markov_tables_data.inc"

// ---------------------------------------------------------------------------
// Model definitions (real Bach reference data)
// ---------------------------------------------------------------------------
// PitchTransitionTable and DurTransitionTable are standard-layout structs
// whose sole member is a uint16_t array with the same dimensions as the
// constexpr arrays in markov_data, so reinterpret_cast is layout-safe.

const MarkovModel kFugueUpperMarkov = {
    "FugueUpper",
    reinterpret_cast<const PitchTransitionTable&>(markov_data::kFugueUpperPitch),
    reinterpret_cast<const DurTransitionTable&>(markov_data::kFugueUpperDur)};
const MarkovModel kFuguePedalMarkov = {
    "FuguePedal",
    reinterpret_cast<const PitchTransitionTable&>(markov_data::kFuguePedalPitch),
    reinterpret_cast<const DurTransitionTable&>(markov_data::kFuguePedalDur)};
const MarkovModel kCelloMarkov = {
    "Cello",
    reinterpret_cast<const PitchTransitionTable&>(markov_data::kCelloPitch),
    reinterpret_cast<const DurTransitionTable&>(markov_data::kCelloDur)};
const MarkovModel kViolinMarkov = {
    "Violin",
    reinterpret_cast<const PitchTransitionTable&>(markov_data::kViolinPitch),
    reinterpret_cast<const DurTransitionTable&>(markov_data::kViolinDur)};

const VerticalIntervalTable kFugueVerticalTable =
    reinterpret_cast<const VerticalIntervalTable&>(markov_data::kFugueVertical);

}  // namespace bach
