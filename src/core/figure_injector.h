// Vocabulary pattern injection for melodic generation.
// Generates candidate pitch sequences from Bach vocabulary figures.
// Candidates are scored by existing scoreCandidatePitch(); adoption is not guaranteed.

#ifndef BACH_CORE_FIGURE_INJECTOR_H
#define BACH_CORE_FIGURE_INJECTOR_H

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "core/bach_vocabulary.h"
#include "core/basic_types.h"
#include "core/melodic_state.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"

namespace bach {

/// @brief A candidate pitch sequence from a vocabulary figure.
struct FigureCandidate {
  std::vector<uint8_t> pitches;   ///< Concrete MIDI pitches.
  const MelodicFigure* source;    ///< Provenance tracking (which figure).
};

/// @brief Resolve a DegreeInterval to a concrete pitch from a starting pitch.
/// @param start_pitch Starting MIDI pitch.
/// @param di Degree interval to apply.
/// @param key Current musical key.
/// @param scale Current scale type.
/// @return Resolved MIDI pitch, or 0 if out of range.
inline uint8_t resolveDegreeInterval(uint8_t start_pitch,
                                     const DegreeInterval& di,
                                     Key key, ScaleType scale) {
  int start_deg = scale_util::pitchToAbsoluteDegree(start_pitch, key, scale);
  int target_deg = start_deg + di.degree_diff;
  uint8_t target_pitch = scale_util::absoluteDegreeToPitch(target_deg, key, scale);
  // Apply chromatic offset.
  int adjusted = static_cast<int>(target_pitch) + di.chroma_offset;
  if (adjusted < 0 || adjusted > 127) return 0;
  return static_cast<uint8_t>(adjusted);
}

/// @brief Try to inject a vocabulary figure as a candidate pitch sequence.
///
/// Selects a figure based on phrase_progress, resolves it to concrete pitches,
/// and validates range constraints. Returns std::nullopt if no valid candidate.
///
/// @param state Current melodic state (for phrase_progress).
/// @param current_pitch Current MIDI pitch (figure starts here).
/// @param key Current musical key.
/// @param scale Current scale type.
/// @param current_tick Current tick position (for beat-position adjustments).
/// @param low_pitch Lower pitch bound.
/// @param high_pitch Upper pitch bound.
/// @param rng Random number generator.
/// @param figure_probability Base probability of attempting injection.
/// @return Optional FigureCandidate if injection succeeds.
inline std::optional<FigureCandidate> tryInjectFigure(
    const MelodicState& state, uint8_t current_pitch,
    Key key, ScaleType scale, Tick current_tick,
    uint8_t low_pitch, uint8_t high_pitch,
    std::mt19937& rng, float figure_probability) {
  // Probabilistic gate with offbeat bonus.
  float effective_prob = figure_probability;
  if (metricLevel(current_tick) == MetricLevel::Offbeat) {
    effective_prob += 0.08f;
  }
  if (!rng::rollProbability(rng, effective_prob)) {
    return std::nullopt;
  }

  // Select figure based on phrase_progress.
  // Early: ascending/turn-up patterns. Mid: neighbors/cambiata. Late: descending.
  const MelodicFigure* candidates_early[] = {
      &kAscRun4, &kTurnUp, &kStepDownLeapUp};
  const MelodicFigure* candidates_mid[] = {
      &kLowerNbr, &kUpperNbr, &kCambiataNbr, &kEchappee};
  const MelodicFigure* candidates_late[] = {
      &kDescRun4, &kCambiataDown, &kLeapUpStepDown, &kTurnDown};

  const MelodicFigure** pool;
  int pool_size;
  float progress = state.phrase_progress;
  if (progress < 0.3f) {
    pool = candidates_early;
    pool_size = 3;
  } else if (progress < 0.6f) {
    pool = candidates_mid;
    pool_size = 4;
  } else {
    pool = candidates_late;
    pool_size = 4;
  }

  const MelodicFigure* figure = pool[rng::rollRange(rng, 0, pool_size - 1)];

  // Resolve figure to concrete pitches.
  if (figure->primary_mode != IntervalMode::Degree || !figure->degree_intervals) {
    return std::nullopt;  // Only degree-mode figures are injectable.
  }

  std::vector<uint8_t> pitches;
  pitches.reserve(figure->note_count);
  pitches.push_back(current_pitch);

  for (int idx = 0; idx < figure->note_count - 1; ++idx) {
    uint8_t prev = pitches.back();
    uint8_t next = resolveDegreeInterval(prev, figure->degree_intervals[idx],
                                         key, scale);
    if (next == 0 || next < low_pitch || next > high_pitch) {
      return std::nullopt;  // Out of range -- abort entire figure.
    }
    pitches.push_back(next);
  }

  return FigureCandidate{std::move(pitches), figure};
}

/// @brief Generate a Fortspinnung gesture: leap up then stepwise descent.
///
/// Bach's characteristic spinning-forth pattern: a skip/leap upward followed
/// by 2-4 stepwise descending notes. Leap size is biased toward 3rds (60%)
/// and 4ths (35%), with 5ths rare (5%) and restricted to bar positions.
///
/// @param current_pitch Starting MIDI pitch.
/// @param key Current musical key.
/// @param scale Current scale type.
/// @param current_tick Current tick position.
/// @param low_pitch Lower pitch bound.
/// @param high_pitch Upper pitch bound.
/// @param rng Random number generator.
/// @return Optional FigureCandidate if generation succeeds.
inline std::optional<FigureCandidate> generateFortspinnung(
    uint8_t current_pitch, Key key, ScaleType scale,
    Tick current_tick, uint8_t low_pitch, uint8_t high_pitch,
    std::mt19937& rng) {
  // Choose leap size: 3rd (60%), 4th (35%), 5th (5% on bar only).
  int leap_degrees;
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  if (roll < 0.60f) {
    leap_degrees = 2;  // 3rd
  } else if (roll < 0.95f) {
    leap_degrees = 3;  // 4th
  } else {
    // 5th: only on bar position.
    if (metricLevel(current_tick) == MetricLevel::Bar) {
      leap_degrees = 4;  // 5th
    } else {
      leap_degrees = 3;  // Fallback to 4th on non-bar positions.
    }
  }

  // Choose descent length: 2 notes (30%), 3 notes (50%), 4 notes (20%).
  int descent_length;
  float roll2 = rng::rollFloat(rng, 0.0f, 1.0f);
  if (roll2 < 0.30f) {
    descent_length = 2;
  } else if (roll2 < 0.80f) {
    descent_length = 3;
  } else {
    descent_length = 4;
  }

  // Resolve ascending leap.
  int start_deg = scale_util::pitchToAbsoluteDegree(current_pitch, key, scale);
  int leap_deg = start_deg + leap_degrees;
  uint8_t leap_pitch = scale_util::absoluteDegreeToPitch(leap_deg, key, scale);
  if (leap_pitch < low_pitch || leap_pitch > high_pitch) {
    return std::nullopt;
  }

  // Build pitch sequence: start -> leap_pitch -> descent...
  std::vector<uint8_t> pitches;
  pitches.reserve(1 + 1 + descent_length);
  pitches.push_back(current_pitch);
  pitches.push_back(leap_pitch);

  // Stepwise descent.
  int current_deg = leap_deg;
  for (int idx = 0; idx < descent_length; ++idx) {
    current_deg -= 1;
    uint8_t desc_pitch = scale_util::absoluteDegreeToPitch(current_deg, key, scale);
    if (desc_pitch < low_pitch || desc_pitch > high_pitch) {
      return std::nullopt;
    }
    pitches.push_back(desc_pitch);
  }

  // Optional: 20% chance to replace last 2 notes with cambiata (kCambiataDown pattern).
  if (pitches.size() >= 4 && rng::rollProbability(rng, 0.20f)) {
    // Cambiata: the last 2 notes become "down, up" instead of "down, down".
    size_t num = pitches.size();
    int second_last_deg = current_deg + 1;  // One step above final note.
    uint8_t cambiata_pitch = scale_util::absoluteDegreeToPitch(
        second_last_deg + 1, key, scale);  // One step above second-to-last.
    if (cambiata_pitch >= low_pitch && cambiata_pitch <= high_pitch) {
      pitches[num - 2] = cambiata_pitch;  // Replace with upward step.
      // Last note stays as stepwise descent target.
    }
  }

  // Static figure reference for provenance.
  static const MelodicFigure kFortspinnungFigure = {
      "fortspinnung", IntervalMode::Degree, true,
      nullptr, nullptr, nullptr, nullptr,
      0, "generated"};

  return FigureCandidate{std::move(pitches), &kFortspinnungFigure};
}

}  // namespace bach

#endif  // BACH_CORE_FIGURE_INJECTOR_H
