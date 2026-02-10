// Fugue subject: data structure and generator for the primary melodic
// material (dux) of a fugue.

#ifndef BACH_FUGUE_SUBJECT_H
#define BACH_FUGUE_SUBJECT_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"

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
  /// @brief Generate the note sequence for a subject.
  /// @param character Subject character type.
  /// @param key Musical key.
  /// @param bars Number of bars (2-4).
  /// @param seed Random seed.
  /// @return Vector of NoteEvent forming the subject.
  std::vector<NoteEvent> generateNotes(SubjectCharacter character,
                                       Key key, bool is_minor,
                                       uint8_t bars,
                                       uint32_t seed) const;
};

}  // namespace bach

#endif  // BACH_FUGUE_SUBJECT_H
