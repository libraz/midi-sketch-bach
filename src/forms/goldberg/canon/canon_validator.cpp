// Post-generation validator for canon integrity in Goldberg Variations.

#include "forms/goldberg/canon/canon_validator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "core/pitch_utils.h"
#include "core/scale.h"
#include "harmony/key.h"

namespace bach {
namespace {

/// @brief Determine the ScaleType from a CanonSpec's key and minor profile.
/// @param spec Canon specification.
/// @return ScaleType for diatonic operations.
ScaleType getScaleTypeFromSpec(const CanonSpec& spec) {
  if (!spec.key.is_minor) {
    return ScaleType::Major;
  }

  switch (spec.minor_profile) {
    case MinorModeProfile::NaturalMinor:
      return ScaleType::NaturalMinor;
    case MinorModeProfile::HarmonicMinor:
      return ScaleType::HarmonicMinor;
    case MinorModeProfile::MixedBaroqueMinor:
      // For diatonic transposition in mixed baroque minor, use natural minor
      // as the base; melodic alterations are context-dependent and handled
      // at the note-selection level, not in the transform.
      return ScaleType::NaturalMinor;
  }

  return ScaleType::NaturalMinor;  // Fallback.
}

/// @brief Transform a dux pitch to expected comes pitch.
///
/// Replicates the DuxBuffer::transformPitch logic:
///   - Regular: diatonic transposition by canon_interval scale degrees.
///   - Inverted: diatonic inversion around tonic, then transposition.
///
/// @param dux_pitch MIDI pitch of the dux note.
/// @param spec Canon specification.
/// @return Expected MIDI pitch for the comes note.
uint8_t computeExpectedComesPitch(uint8_t dux_pitch, const CanonSpec& spec) {
  Key key = spec.key.tonic;
  ScaleType scale = getScaleTypeFromSpec(spec);

  if (spec.transform == CanonTransform::Regular) {
    int dux_degree = scale_util::pitchToAbsoluteDegree(dux_pitch, key, scale);
    int comes_degree = dux_degree + spec.canon_interval;
    return scale_util::absoluteDegreeToPitch(comes_degree, key, scale);
  }

  // CanonTransform::Inverted:
  // Step 1: Invert diatonically around the tonic.
  // Step 2: Transpose by canon_interval scale degrees.
  int musical_octave = static_cast<int>(dux_pitch) / 12 - 1;
  uint8_t tonic_pitch = tonicPitch(key, musical_octave);
  int tonic_degree = scale_util::pitchToAbsoluteDegree(tonic_pitch, key, scale);
  int dux_degree = scale_util::pitchToAbsoluteDegree(dux_pitch, key, scale);

  // Inversion: reflect around tonic degree.
  int inverted_degree = 2 * tonic_degree - dux_degree;

  // Transpose by canon_interval.
  int comes_degree = inverted_degree + spec.canon_interval;
  return scale_util::absoluteDegreeToPitch(comes_degree, key, scale);
}

/// @brief Find the note in a vector closest to the target tick.
///
/// Returns the index of the note whose start_tick is within tolerance of
/// target_tick, or -1 if no such note exists.
///
/// @param notes Note vector to search.
/// @param target_tick Target tick position.
/// @param tolerance Maximum tick difference for a match.
/// @return Index of the matching note, or -1.
int findNoteAtTick(const std::vector<NoteEvent>& notes, Tick target_tick,
                   Tick tolerance) {
  int best_idx = -1;
  Tick best_diff = tolerance + 1;

  for (int idx = 0; idx < static_cast<int>(notes.size()); ++idx) {
    Tick diff = (notes[idx].start_tick >= target_tick)
                    ? notes[idx].start_tick - target_tick
                    : target_tick - notes[idx].start_tick;
    if (diff <= tolerance && diff < best_diff) {
      best_diff = diff;
      best_idx = idx;
    }
  }

  return best_idx;
}

/// @brief Find the melodic peak (highest pitch) in a note vector.
/// @param notes Notes to search.
/// @return Pair of (bar number, pitch) for the highest note. Bar is computed
///         using the given ticks_per_bar. Returns (-1, 0) for empty input.
std::pair<int, uint8_t> findMelodicPeak(const std::vector<NoteEvent>& notes,
                                        Tick ticks_per_bar) {
  if (notes.empty()) {
    return {-1, 0};
  }

  uint8_t max_pitch = 0;
  Tick peak_tick = 0;

  for (const auto& note : notes) {
    if (note.pitch > max_pitch) {
      max_pitch = note.pitch;
      peak_tick = note.start_tick;
    }
  }

  int peak_bar = (ticks_per_bar > 0)
                     ? static_cast<int>(peak_tick / ticks_per_bar)
                     : 0;
  return {peak_bar, max_pitch};
}

/// @brief Check if a bar is within distance of any Intensification position.
/// @param bar Bar index (0-based).
/// @param grid Structural grid.
/// @param max_distance Maximum bar distance for a match.
/// @return True if bar is within max_distance of an Intensification bar.
bool isNearIntensification(int bar, const GoldbergStructuralGrid& grid,
                           int max_distance) {
  for (int idx = 0; idx < 32; ++idx) {
    if (grid.getPhrasePosition(idx) == PhrasePosition::Intensification) {
      if (std::abs(bar - idx) <= max_distance) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

CanonValidationResult validateCanonIntegrity(
    const std::vector<NoteEvent>& dux_notes,
    const std::vector<NoteEvent>& comes_notes,
    const CanonSpec& spec,
    const TimeSignature& time_sig) {
  CanonValidationResult result;

  // Empty inputs: trivially pass with zero pairs.
  if (dux_notes.empty() || comes_notes.empty()) {
    result.total_pairs = 0;
    result.pitch_accuracy = 1.0f;
    result.passed = true;
    return result;
  }

  Tick delay_ticks = static_cast<Tick>(spec.delay_bars) * time_sig.ticksPerBar();
  constexpr Tick kTimingTolerance = 1;  // +/-1 tick for rounding.

  for (const auto& dux_note : dux_notes) {
    Tick expected_comes_tick = dux_note.start_tick + delay_ticks;
    int comes_idx = findNoteAtTick(comes_notes, expected_comes_tick,
                                   kTimingTolerance);

    if (comes_idx < 0) {
      // No matching comes note found at the expected tick.
      // This could be due to the comes not having entered yet, so only
      // count as a violation if the expected tick is within the comes range.
      if (!comes_notes.empty() &&
          expected_comes_tick >= comes_notes.front().start_tick &&
          expected_comes_tick <= comes_notes.back().start_tick + kTicksPerBeat) {
        ++result.timing_violations;
        result.messages.push_back(
            "Timing: no comes note at tick " +
            std::to_string(expected_comes_tick) +
            " for dux note at tick " +
            std::to_string(dux_note.start_tick));
      }
      continue;
    }

    const NoteEvent& comes_note = comes_notes[comes_idx];
    ++result.total_pairs;

    // Check timing (within tolerance).
    Tick tick_diff = (comes_note.start_tick >= expected_comes_tick)
                         ? comes_note.start_tick - expected_comes_tick
                         : expected_comes_tick - comes_note.start_tick;
    if (tick_diff > kTimingTolerance) {
      ++result.timing_violations;
      result.messages.push_back(
          "Timing: comes at tick " +
          std::to_string(comes_note.start_tick) +
          " expected " + std::to_string(expected_comes_tick) +
          " (diff=" + std::to_string(tick_diff) + ")");
    }

    // Check pitch transformation.
    uint8_t expected_pitch = computeExpectedComesPitch(dux_note.pitch, spec);
    if (comes_note.pitch != expected_pitch) {
      ++result.pitch_violations;
      result.messages.push_back(
          "Pitch: dux=" + pitchToNoteName(dux_note.pitch) +
          "(" + std::to_string(dux_note.pitch) + ")" +
          " comes=" + pitchToNoteName(comes_note.pitch) +
          "(" + std::to_string(comes_note.pitch) + ")" +
          " expected=" + pitchToNoteName(expected_pitch) +
          "(" + std::to_string(expected_pitch) + ")" +
          " at tick " + std::to_string(dux_note.start_tick));
    }

    // Check duration (for StrictRhythm mode).
    if (spec.rhythmic_mode == CanonRhythmicMode::StrictRhythm) {
      if (comes_note.duration != dux_note.duration) {
        ++result.duration_violations;
        result.messages.push_back(
            "Duration: dux=" + std::to_string(dux_note.duration) +
            " comes=" + std::to_string(comes_note.duration) +
            " at tick " + std::to_string(dux_note.start_tick));
      }
    }
  }

  // Compute pitch accuracy.
  if (result.total_pairs > 0) {
    int correct_pitches = result.total_pairs - result.pitch_violations;
    result.pitch_accuracy =
        static_cast<float>(correct_pitches) / static_cast<float>(result.total_pairs);
  } else {
    result.pitch_accuracy = 1.0f;
  }

  // Pass if pitch accuracy >= 95% and no timing or duration violations.
  result.passed = result.pitch_accuracy >= 0.95f &&
                  result.timing_violations == 0 &&
                  result.duration_violations == 0;

  return result;
}

bool validateClimaxAlignment(
    const std::vector<NoteEvent>& dux_notes,
    const std::vector<NoteEvent>& comes_notes,
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig) {
  Tick ticks_per_bar = time_sig.ticksPerBar();

  auto [dux_bar, dux_peak] = findMelodicPeak(dux_notes, ticks_per_bar);
  auto [comes_bar, comes_peak] = findMelodicPeak(comes_notes, ticks_per_bar);

  // If either voice is empty, alignment is vacuously true.
  if (dux_bar < 0 || comes_bar < 0) {
    return true;
  }

  // Clamp to grid range [0, 31].
  constexpr int kMaxBar = 31;
  constexpr int kAlignmentTolerance = 2;

  int clamped_dux_bar = std::max(0, std::min(dux_bar, kMaxBar));
  int clamped_comes_bar = std::max(0, std::min(comes_bar, kMaxBar));

  bool dux_aligned = isNearIntensification(clamped_dux_bar, grid,
                                           kAlignmentTolerance);
  bool comes_aligned = isNearIntensification(clamped_comes_bar, grid,
                                             kAlignmentTolerance);

  return dux_aligned && comes_aligned;
}

}  // namespace bach
