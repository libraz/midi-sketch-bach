// FigurenGenerator for Goldberg Variations Elaboratio melody generation.

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_FIGUREN_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_FIGUREN_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"
#include "instrument/keyboard/keyboard_instrument.h"

namespace bach {

/// @brief Phrase-level parameter adjustments based on PhrasePosition.
///
/// Each phrase position modulates the base FiguraProfile parameters to shape
/// melodic contour across the 4-bar phrase arc.
struct PhraseShapingParams {
  float density_multiplier;          ///< Density scaling (1.0 = profile value).
  float range_expansion;             ///< Range expansion in semitones.
  float chord_tone_shift;            ///< Chord tone ratio shift.
  DirectionBias direction_override;  ///< Symmetric = use profile value.
};

/// @brief Returns default shaping params for a given phrase position.
/// @param pos The phrase position within a 4-bar group.
/// @return PhraseShapingParams with design-value defaults.
PhraseShapingParams getDefaultPhraseShaping(PhrasePosition pos);

/// @brief Generates 32-bar melodies by placing Figura patterns onto the structural grid.
///
/// Each variation has a dominant Figura that shapes all bars. The generator
/// reads harmonic pivot pitches from the structural grid and applies
/// Figurenlehre patterns (Circulatio, Tirata, Batterie, etc.) to produce
/// idiomatic Baroque melodic material.
class FigurenGenerator {
 public:
  /// @brief Generate 32 bars of melody from Figura patterns on the structural grid.
  /// @param profile The FiguraProfile defining pattern type, density, and direction.
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature for scale-aware pitch generation.
  /// @param time_sig Time signature for tick calculation.
  /// @param voice_index Voice index (0 = upper, higher = lower register).
  /// @param seed Random seed for deterministic generation.
  /// @param instrument Optional keyboard instrument for range constraints.
  /// @return Vector of NoteEvents spanning 32 bars.
  std::vector<NoteEvent> generate(
      const FiguraProfile& profile,
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint8_t voice_index,
      uint32_t seed,
      const IKeyboardInstrument* instrument = nullptr,
      float theme_strength = 0.0f) const;

 private:
  /// @brief Generate one bar of Figura pattern.
  /// @param profile The FiguraProfile for this variation.
  /// @param bar_info Structural information for the current bar.
  /// @param key Key signature for scale context.
  /// @param time_sig Time signature for duration calculation.
  /// @param prev_pitch Previous bar's last pitch for melodic continuity.
  /// @param register_center Target register center pitch (constant anchor).
  /// @param shaping Phrase-level parameter adjustments.
  /// @param rng Random number generator.
  /// @return Vector of NoteEvents for one bar.
  std::vector<NoteEvent> generateBarFigura(
      const FiguraProfile& profile,
      const StructuralBarInfo& bar_info,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint8_t prev_pitch,
      uint8_t register_center,
      const PhraseShapingParams& shaping,
      uint8_t range_low,
      uint8_t range_high,
      float theme_strength,
      std::mt19937& rng) const;

  /// @brief Generate pitch pattern for a specific FiguraType from a harmonic pivot.
  /// @param type The Figura type to generate.
  /// @param pivot_pitch Harmonic pivot pitch (MIDI) from the structural grid.
  /// @param notes_per_beat Number of notes per beat.
  /// @param direction Direction bias for the pattern.
  /// @param key Key signature for scale-aware generation.
  /// @param rng Random number generator.
  /// @return Vector of MIDI pitches forming the pattern.
  std::vector<uint8_t> generateFiguraPattern(
      FiguraType type,
      uint8_t pivot_pitch,
      uint8_t notes_per_beat,
      DirectionBias direction,
      const KeySignature& key,
      int register_offset,
      uint8_t range_low,
      uint8_t range_high,
      std::mt19937& rng) const;

  /// @brief Apply phrase shaping adjustments to generated notes.
  /// @param notes Notes to adjust (modified in place).
  /// @param pos Current phrase position.
  /// @param tension Tension profile for the current bar.
  void applyPhraseShaping(
      std::vector<NoteEvent>& notes,
      PhrasePosition pos,
      const TensionProfile& tension) const;

  /// @brief Apply sequential repetition (Zeugma) based on sequence_probability.
  /// @param motif Source melodic fragment.
  /// @param degree_step Scale-degree step for each transposition.
  /// @param repetitions Number of sequential repetitions.
  /// @param key Key signature for diatonic transposition.
  /// @return Sequenced notes (does not include original motif).
  std::vector<NoteEvent> applySequence(
      const std::vector<NoteEvent>& motif,
      int degree_step,
      int repetitions,
      const KeySignature& key) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_FIGUREN_H
