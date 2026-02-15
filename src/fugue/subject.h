// Fugue subject: data structure and generator for the primary melodic
// material (dux) of a fugue.

#ifndef BACH_FUGUE_SUBJECT_H
#define BACH_FUGUE_SUBJECT_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/subject_identity.h"

namespace bach {

struct SubjectScore;  // Forward declaration (see subject_validator.h).

/// The fugue subject (dux): the primary melodic material from which the
/// entire fugue is derived.
struct Subject {
  std::vector<NoteEvent> notes;
  Key key = Key::C;
  bool is_minor = false;  ///< Whether the home key is minor.
  Tick length_ticks = 0;  ///< Total length including anacrusis.
  SubjectCharacter character = SubjectCharacter::Severe;
  Tick anacrusis_ticks = 0;  ///< Duration of the anacrusis (0 = no anacrusis).
  SubjectIdentity identity;  ///< Kerngestalt identity (Layers 1+2, immutable after generate).
  CellWindow cell_window;  ///< Kerngestalt cell window (shortcut, also in identity.essential).

  /// @brief Get the lowest MIDI pitch in the subject.
  /// @return Lowest pitch, or 127 if subject is empty.
  uint8_t lowestPitch() const;

  /// @brief Get the highest MIDI pitch in the subject.
  /// @return Highest pitch, or 0 if subject is empty.
  uint8_t highestPitch() const;

  /// @brief Get the pitch range in semitones.
  /// @return highestPitch() - lowestPitch(), or 0 if empty.
  int range() const;

  /// @brief Get the number of notes in the subject.
  /// @return Note count.
  size_t noteCount() const;

  /// @brief Extract the head motif (Kopfmotiv) from the subject.
  ///
  /// Returns the first max_notes notes, which form the characteristic opening
  /// fragment used in episode development (Baroque Fortspinnung technique).
  ///
  /// @param max_notes Maximum notes to extract (default: 4).
  /// @return Vector of the first min(max_notes, noteCount()) notes.
  std::vector<NoteEvent> extractKopfmotiv(size_t max_notes = 4) const;
};

/// @brief Generates fugue subjects based on character and configuration.
///
/// The generator creates melodic material that serves as the dux (leader)
/// for a fugue. Subjects are constrained by character type:
///   - Severe: stepwise, narrow range, even rhythms
///   - Playful: more leaps, dotted rhythms, wider range
///   - Noble: stately motion, moderate range
///   - Restless: chromatic tendencies, irregular rhythm
class SubjectGenerator {
 public:
  /// @brief Generate a fugue subject from the given configuration.
  /// @param config Fugue configuration (character, key, bars, etc.).
  /// @param seed Random seed for deterministic generation.
  /// @return Generated Subject.
  Subject generate(const FugueConfig& config, uint32_t seed) const;

 private:
  /// @brief Result of note generation including cell window metadata.
  struct GenerateResult {
    std::vector<NoteEvent> notes;
    CellWindow cell_window;
  };

  /// @brief Generate the note sequence for a subject.
  /// @param config Fugue configuration (character, key, archetype, etc.).
  /// @param bars Number of bars (1-4, already clamped by archetype).
  /// @param seed Random seed.
  /// @return GenerateResult with notes and cell window.
  GenerateResult generateNotes(const FugueConfig& config,
                               uint8_t bars,
                               uint32_t seed) const;
};

// ---------------------------------------------------------------------------
// Shared subject/answer pitch helpers
// ---------------------------------------------------------------------------

/// @brief Maximum allowed leap in semitones for the given character.
/// @param ch Subject character type.
/// @return Maximum leap in semitones (7 for Severe/Noble, 9 for Playful/Restless).
int maxLeapForCharacter(SubjectCharacter ch);

/// @brief Normalize an ending pitch to be reachable from the previous note.
///
/// Finds the nearest octave of the target pitch class within max_leap of
/// prev_pitch. Falls back to stepwise approach if no direct octave works.
/// Used by both Subject and Answer ending normalization.
///
/// @param target_pitch_class Target pitch class (0-11).
/// @param prev_pitch Previous note's MIDI pitch.
/// @param max_leap Maximum allowed leap in semitones.
/// @param key Musical key.
/// @param scale Scale type.
/// @param floor Minimum allowed pitch.
/// @param ceil Maximum allowed pitch.
/// @return Normalized ending pitch.
int normalizeEndingPitch(int target_pitch_class, int prev_pitch,
                         int max_leap, Key key, ScaleType scale,
                         int floor, int ceil);

}  // namespace bach

#endif  // BACH_FUGUE_SUBJECT_H
