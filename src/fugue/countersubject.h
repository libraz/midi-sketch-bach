// Countersubject generation for fugue exposition: a melodic line that
// accompanies the subject with contrary motion and complementary rhythm.

#ifndef BACH_FUGUE_COUNTERSUBJECT_H
#define BACH_FUGUE_COUNTERSUBJECT_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/subject.h"

namespace bach {

/// The countersubject: a melodic line that accompanies the subject throughout
/// the fugue exposition and subsequent entries. It maintains its own identity
/// through contrary motion, complementary rhythm, and consonant intervals
/// with the subject.
struct Countersubject {
  std::vector<NoteEvent> notes;
  Key key = Key::C;
  Tick length_ticks = 0;

  /// @brief Get the number of notes in the countersubject.
  /// @return Note count.
  size_t noteCount() const;

  /// @brief Get the lowest MIDI pitch in the countersubject.
  /// @return Lowest pitch, or 127 if empty.
  uint8_t lowestPitch() const;

  /// @brief Get the highest MIDI pitch in the countersubject.
  /// @return Highest pitch, or 0 if empty.
  uint8_t highestPitch() const;

  /// @brief Get the pitch range in semitones.
  /// @return highestPitch() - lowestPitch(), or 0 if empty.
  int range() const;
};

/// @brief Generate a countersubject for the given subject.
///
/// The countersubject moves contrary to the subject (when subject ascends,
/// countersubject descends), uses complementary rhythm, and maintains
/// consonant intervals. Character influence:
///   - Severe subject: contrasting but restrained countersubject (stepwise)
///   - Playful subject: freer, more leaps allowed, wider range
///   - Noble subject: stately contrary motion, moderate range
///   - Restless subject: more chromatic, wider leaps
///
/// @param subject The fugue subject to write a countersubject against.
/// @param seed Random seed for deterministic generation.
/// @param max_retries Maximum generation attempts (default 5).
/// @return Generated Countersubject.
Countersubject generateCountersubject(
    const Subject& subject, uint32_t seed, int max_retries = 5,
    FugueArchetype archetype = FugueArchetype::Compact);

/// @brief Generate a second countersubject for 4+ voice fugues.
///
/// The second countersubject must contrast with both the subject and the
/// first countersubject. It achieves this by:
///   - Using a different pitch register (higher if CS1 is below subject,
///     lower if CS1 is above)
///   - Employing complementary rhythm to both subject and CS1
///   - Maintaining consonant intervals with both existing lines
///   - Avoiding parallel 5ths/8ths with both subject and CS1
///
/// @param subject The fugue subject.
/// @param first_cs The first countersubject already generated.
/// @param seed Random seed for deterministic generation.
/// @param max_retries Maximum generation attempts (default 5).
/// @return Generated second Countersubject.
Countersubject generateSecondCountersubject(
    const Subject& subject, const Countersubject& first_cs,
    uint32_t seed, int max_retries = 5,
    FugueArchetype archetype = FugueArchetype::Compact);

/// @brief Adapt countersubject notes to a target key.
///
/// Snaps any non-scale pitches to the nearest scale tone in the target key.
/// Used when placing the countersubject against entries in different keys
/// (exposition answer entries, middle entries in related keys).
///
/// @param cs_notes Countersubject notes to adapt.
/// @param to_key Target key.
/// @param scale Scale type (default Major).
/// @return Adapted notes with pitches snapped to the target key.
std::vector<NoteEvent> adaptCSToKey(const std::vector<NoteEvent>& cs_notes,
                                     Key to_key,
                                     ScaleType scale = ScaleType::Major);

}  // namespace bach

#endif  // BACH_FUGUE_COUNTERSUBJECT_H
