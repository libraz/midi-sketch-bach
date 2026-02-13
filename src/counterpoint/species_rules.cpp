/// @file
/// @brief Implementation of species-specific counterpoint rules (1st through 5th species).

#include "counterpoint/species_rules.h"

#include <cstdlib>

#include "core/pitch_utils.h"

namespace bach {

// ---------------------------------------------------------------------------
// SpeciesType to string
// ---------------------------------------------------------------------------

/// @brief Convert a SpeciesType enum value to its string representation.
/// @param species The species type to convert.
/// @return A C-string name for the species (e.g. "first_species").
const char* speciesToString(SpeciesType species) {
  switch (species) {
    case SpeciesType::First:  return "first_species";
    case SpeciesType::Second: return "second_species";
    case SpeciesType::Third:  return "third_species";
    case SpeciesType::Fourth: return "fourth_species";
    case SpeciesType::Fifth:  return "fifth_species";
  }
  return "unknown_species";
}

// ---------------------------------------------------------------------------
// SpeciesRules
// ---------------------------------------------------------------------------

SpeciesRules::SpeciesRules(SpeciesType species) : species_(species) {}

SpeciesType SpeciesRules::getSpecies() const { return species_; }

int SpeciesRules::notesPerBeat() const {
  switch (species_) {
    case SpeciesType::First:  return 1;
    case SpeciesType::Second: return 2;
    case SpeciesType::Third:  return 4;
    case SpeciesType::Fourth: return 1;  // Syncopated, same ratio as 1st.
    case SpeciesType::Fifth:  return 0;  // Varies (florid).
  }
  return 1;
}

bool SpeciesRules::isDissonanceAllowed(bool is_strong_beat) const {
  switch (species_) {
    case SpeciesType::First:
      // First species: only consonances, on every beat.
      return false;

    case SpeciesType::Second:
      // Second species: consonance required on strong beats.
      // Dissonance allowed on weak beats (passing tones).
      return !is_strong_beat;

    case SpeciesType::Third:
      // Third species: consonance on beat 1.  Dissonance allowed on
      // beats 2, 3, 4 as passing/neighbor tones.
      return !is_strong_beat;

    case SpeciesType::Fourth:
      // Fourth species (syncopation): the tied-over note on the strong
      // beat may be dissonant (suspension), provided it resolves
      // downward by step on the weak beat.
      return is_strong_beat;  // Suspension occurs on the strong beat.

    case SpeciesType::Fifth:
      // Fifth species (florid): combines all species.  Dissonance is
      // context-dependent -- allow it on weak beats and as suspensions.
      return !is_strong_beat;
  }
  return false;
}

bool SpeciesRules::requiresSuspensionResolution() const {
  return species_ == SpeciesType::Fourth;
}

// ---------------------------------------------------------------------------
// Stepwise motion check
// ---------------------------------------------------------------------------

bool SpeciesRules::isStep(int semitones) {
  int abs_interval = std::abs(semitones);
  return abs_interval == 1 || abs_interval == 2;
}

// ---------------------------------------------------------------------------
// Passing tone validation
// ---------------------------------------------------------------------------

bool SpeciesRules::isValidPassingTone(uint8_t prev, uint8_t current,
                                      uint8_t next) const {
  // Passing tones are not allowed in first species.
  if (species_ == SpeciesType::First) return false;

  int interval_in = static_cast<int>(current) - static_cast<int>(prev);
  int interval_out = static_cast<int>(next) - static_cast<int>(current);

  // Both intervals must be stepwise.
  if (!isStep(interval_in) || !isStep(interval_out)) return false;

  // Direction must be the same (ascending through or descending through).
  // Both positive (ascending) or both negative (descending).
  if ((interval_in > 0) != (interval_out > 0)) return false;

  return true;
}

// ---------------------------------------------------------------------------
// Neighbor tone validation
// ---------------------------------------------------------------------------

bool SpeciesRules::isValidNeighborTone(uint8_t prev, uint8_t current,
                                       uint8_t next) const {
  // Neighbor tones appear in 3rd species and above.
  if (species_ == SpeciesType::First || species_ == SpeciesType::Second) {
    return false;
  }

  // A neighbor tone departs by step and returns to the original pitch.
  if (prev != next) return false;

  int interval = static_cast<int>(current) - static_cast<int>(prev);
  return isStep(interval);
}

// ---------------------------------------------------------------------------
// Non-harmonic tone classification
// ---------------------------------------------------------------------------

NonHarmonicToneType classifyNonHarmonicTone(uint8_t prev_pitch, uint8_t current_pitch,
                                             std::optional<uint8_t> next_pitch,
                                             bool is_chord_tone,
                                             bool prev_is_chord_tone,
                                             bool next_is_chord_tone) {
  if (is_chord_tone) return NonHarmonicToneType::ChordTone;

  bool has_prev = prev_pitch > 0;
  bool has_next = next_pitch.has_value();

  if (has_prev && has_next) {
    int step_from_prev = absoluteInterval(current_pitch, prev_pitch);
    int step_to_next = absoluteInterval(*next_pitch, current_pitch);

    // Passing tone: stepwise from prev, stepwise to next, same direction,
    // both neighbors are chord tones.
    if (step_from_prev <= 2 && step_to_next <= 2 && prev_is_chord_tone &&
        next_is_chord_tone) {
      int dir1 = static_cast<int>(current_pitch) - static_cast<int>(prev_pitch);
      int dir2 = static_cast<int>(*next_pitch) - static_cast<int>(current_pitch);
      if ((dir1 > 0 && dir2 > 0) || (dir1 < 0 && dir2 < 0)) {
        return NonHarmonicToneType::PassingTone;
      }
    }

    // Neighbor tone: step away then return to same pitch.
    if (step_from_prev <= 2 && prev_pitch == *next_pitch && prev_is_chord_tone) {
      return NonHarmonicToneType::NeighborTone;
    }
  }

  // Suspension: held from previous beat (same pitch as prev), resolves down by step.
  if (has_prev && prev_pitch == current_pitch && has_next) {
    int resolution = static_cast<int>(current_pitch) - static_cast<int>(*next_pitch);
    if (resolution >= 1 && resolution <= 2 && next_is_chord_tone) {
      return NonHarmonicToneType::Suspension;
    }
  }

  if (has_prev && has_next) {
    int step_from_prev = absoluteInterval(current_pitch, prev_pitch);
    int step_to_next = absoluteInterval(*next_pitch, current_pitch);
    int dir_in = static_cast<int>(current_pitch) - static_cast<int>(prev_pitch);
    int dir_out = static_cast<int>(*next_pitch) - static_cast<int>(current_pitch);

    // Escape tone: stepwise entry, leap exit in opposite direction.
    if (step_from_prev <= 2 && step_to_next >= 3 && prev_is_chord_tone) {
      if ((dir_in > 0 && dir_out < 0) || (dir_in < 0 && dir_out > 0)) {
        return NonHarmonicToneType::EscapeTone;
      }
    }

    // Anticipation: next chord tone sounded early, entered by step.
    if (step_from_prev <= 2 && next_is_chord_tone &&
        current_pitch == *next_pitch) {
      return NonHarmonicToneType::Anticipation;
    }

    // Changing tone (double neighbor): step in one direction, leap to other side.
    if (step_from_prev <= 2 && step_to_next <= 2 && prev_is_chord_tone &&
        next_is_chord_tone && prev_pitch != *next_pitch) {
      if ((dir_in > 0 && dir_out < 0) || (dir_in < 0 && dir_out > 0)) {
        return NonHarmonicToneType::ChangingTone;
      }
    }
  }

  return NonHarmonicToneType::Unknown;
}

}  // namespace bach
