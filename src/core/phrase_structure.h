// Phrase structure: musical phrasing, breathing points, and phrase boundaries.

#ifndef BACH_CORE_PHRASE_STRUCTURE_H
#define BACH_CORE_PHRASE_STRUCTURE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief A musical phrase with start/end boundaries and structural role.
struct Phrase {
  Tick start_tick = 0;   ///< Start of the phrase (inclusive).
  Tick end_tick = 0;     ///< End of the phrase (exclusive).

  /// Structural role of the phrase in antecedent-consequent pairs.
  enum class Role : uint8_t {
    Antecedent,   // Opening phrase (typically ends on V)
    Consequent,   // Answering phrase (typically ends on I)
    Free          // Free phrase (not part of a paired structure)
  };
  Role role = Role::Free;

  /// @brief Get the duration of this phrase in ticks.
  Tick durationTicks() const { return end_tick - start_tick; }
};

/// @brief A breathing point (Atempause) between phrases.
///
/// A brief silence across all voices at a phrase boundary, creating
/// the articulated phrasing characteristic of Bach's keyboard style.
struct BreathingPoint {
  Tick tick = 0;       ///< Position of the breath.
  Tick duration = 0;   ///< Length of the breath in ticks (typically 60-240).
};

/// @brief Phrase structure analysis for a musical passage.
///
/// Infers phrase boundaries from cadence positions and section structure,
/// then generates breathing points at appropriate locations.
class PhraseStructure {
 public:
  /// @brief Build phrase structure from cadence tick positions.
  ///
  /// Divides the total duration into phrases based on cadence positions.
  /// Cadence positions mark phrase endings. Phrases between cadences
  /// are classified as Antecedent/Consequent pairs where possible.
  ///
  /// @param cadence_ticks Tick positions of cadences (sorted ascending).
  /// @param total_duration Total duration of the passage in ticks.
  /// @return PhraseStructure with phrases and breathing points.
  static PhraseStructure fromCadenceTicks(const std::vector<Tick>& cadence_ticks,
                                           Tick total_duration);

  /// @brief Get all phrases.
  /// @return Const reference to the phrase vector.
  const std::vector<Phrase>& phrases() const { return phrases_; }

  /// @brief Get all breathing points.
  /// @return Const reference to the breathing point vector.
  const std::vector<BreathingPoint>& breathingPoints() const {
    return breathing_points_;
  }

  /// @brief Get the distance from a tick to the nearest phrase boundary.
  /// @param tick The tick position to query.
  /// @return Distance in ticks to the nearest phrase start or end.
  ///         Returns 0 if tick is exactly on a boundary.
  Tick distanceToPhraseBoundary(Tick tick) const;

  /// @brief Get the number of phrases.
  size_t phraseCount() const { return phrases_.size(); }

 private:
  std::vector<Phrase> phrases_;
  std::vector<BreathingPoint> breathing_points_;
};

}  // namespace bach

#endif  // BACH_CORE_PHRASE_STRUCTURE_H
