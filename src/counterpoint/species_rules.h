// Species-specific counterpoint rules (Fux species I-V).

#ifndef BACH_COUNTERPOINT_SPECIES_RULES_H
#define BACH_COUNTERPOINT_SPECIES_RULES_H

#include <cstdint>
#include <optional>

namespace bach {

/// @brief Species type for Fux counterpoint.
enum class SpeciesType : uint8_t {
  First,   // 1:1, note against note
  Second,  // 2:1, two notes against one
  Third,   // 4:1, four notes against one
  Fourth,  // Syncopation (suspension/resolution)
  Fifth    // Florid (free combination of all species)
};

/// @brief Type of non-harmonic tone in a melodic context.
enum class NonHarmonicToneType : uint8_t {
  ChordTone,     ///< Pitch belongs to the current chord.
  PassingTone,   ///< Stepwise motion connecting two different chord tones.
  NeighborTone,  ///< Step away from and back to a chord tone.
  Suspension,    ///< Held from previous beat, dissonant on current, resolves down by step.
  Unknown,       ///< Unclassified dissonance.
  EscapeTone,    ///< Stepwise entry, leap exit in opposite direction.
  Anticipation,  ///< Next chord tone sounded early, entered by step.
  ChangingTone,  ///< Double neighbor: upper->lower->main or lower->upper->main.
  PedalTone      ///< Sustained bass note held through changing harmonies.
};

/// @brief Convert SpeciesType to a human-readable string.
/// @param species The species type value.
/// @return Null-terminated C string (e.g. "first_species").
const char* speciesToString(SpeciesType species);

/// @brief Species-specific counterpoint constraints.
///
/// Each species defines how many notes occur per cantus firmus note,
/// when dissonance is permitted, and what melodic patterns are valid
/// for non-harmonic tones (passing tones, neighbor tones, suspensions).
class SpeciesRules {
 public:
  /// @brief Construct rules for a specific species.
  /// @param species The species type.
  explicit SpeciesRules(SpeciesType species);

  /// @brief Get the species type.
  SpeciesType getSpecies() const;

  /// @brief Number of counterpoint notes per cantus firmus beat.
  /// First=1, Second=2, Third=4, Fourth=1 (syncopated), Fifth=varies.
  int notesPerBeat() const;

  /// @brief Whether dissonance is allowed at a given beat position.
  /// @param is_strong_beat True on beats 1 and 3 (4/4 time).
  /// @return True if dissonance is permitted at that beat strength.
  bool isDissonanceAllowed(bool is_strong_beat) const;

  /// @brief Whether this species requires suspension resolution.
  /// True only for Fourth species (syncopated counterpoint).
  bool requiresSuspensionResolution() const;

  /// @brief Validate a passing tone pattern (stepwise through dissonance).
  ///
  /// A valid passing tone approaches and leaves by step (1-2 semitones)
  /// in the same direction.
  ///
  /// @param prev MIDI pitch before the potential passing tone.
  /// @param current MIDI pitch of the potential passing tone.
  /// @param next MIDI pitch after the potential passing tone.
  /// @return True if the pattern is a valid passing tone.
  bool isValidPassingTone(uint8_t prev, uint8_t current,
                          uint8_t next) const;

  /// @brief Validate a neighbor tone pattern (step away and return).
  ///
  /// A valid neighbor tone leaves by step (1-2 semitones) and returns
  /// to the original pitch.
  ///
  /// @param prev MIDI pitch before the potential neighbor tone.
  /// @param current MIDI pitch of the potential neighbor tone.
  /// @param next MIDI pitch after the potential neighbor tone.
  /// @return True if the pattern is a valid neighbor tone.
  bool isValidNeighborTone(uint8_t prev, uint8_t current,
                           uint8_t next) const;

 private:
  SpeciesType species_;

  /// @brief Check if an interval is stepwise (1 or 2 semitones).
  static bool isStep(int semitones);
};

/// @brief Classify a non-harmonic tone based on melodic context.
///
/// Given a three-note window (prev, current, next) and chord-tone membership
/// flags, determines whether the current pitch functions as a passing tone,
/// neighbor tone, suspension, or unclassified dissonance.
///
/// @param prev_pitch Previous MIDI pitch (0 if none).
/// @param current_pitch Current MIDI pitch being classified.
/// @param next_pitch Next MIDI pitch (0 if unknown).
/// @param is_chord_tone True if current_pitch belongs to the current chord.
/// @param prev_is_chord_tone True if prev_pitch belongs to the current chord.
/// @param next_is_chord_tone True if next_pitch belongs to the current chord.
/// @return The classified non-harmonic tone type.
NonHarmonicToneType classifyNonHarmonicTone(uint8_t prev_pitch, uint8_t current_pitch,
                                             std::optional<uint8_t> next_pitch,
                                             bool is_chord_tone,
                                             bool prev_is_chord_tone,
                                             bool next_is_chord_tone);

}  // namespace bach

#endif  // BACH_COUNTERPOINT_SPECIES_RULES_H
