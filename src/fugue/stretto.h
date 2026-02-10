// Stretto: dense overlapping subject entries for the fugue climax.

#ifndef BACH_FUGUE_STRETTO_H
#define BACH_FUGUE_STRETTO_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/subject.h"

namespace bach {

/// @brief A single stretto voice entry.
///
/// Each entry represents one voice's presentation of the subject
/// within the stretto section. Entries overlap with each other,
/// creating the dense imitative texture characteristic of stretto.
struct StrettoEntry {
  VoiceId voice_id = 0;         ///< Which voice presents this entry.
  Tick entry_tick = 0;           ///< When this voice enters.
  std::vector<NoteEvent> notes;  ///< Notes for this entry.
};

/// @brief Complete stretto section with overlapping entries.
///
/// In a stretto, voices enter with the subject at shorter intervals than
/// the full subject length, creating dense overlapping imitation. The
/// stretto belongs to FuguePhase::Resolve and typically uses the home key.
///
/// Design values are output directly (Principle 4: Trust Design Values):
///   - Entry interval: subject_length / num_voices (minimum 1 bar)
///   - All entries in home key
///   - Entry count matches num_voices
///   - Odd-indexed entries use inversion for variety
struct Stretto {
  std::vector<StrettoEntry> entries;  ///< Voice entries in order of appearance.
  Tick start_tick = 0;                ///< Start of the stretto section.
  Tick end_tick = 0;                  ///< End of the stretto section.
  Key key = Key::C;                   ///< Key (home key in Resolve phase).

  /// @brief Get duration in ticks.
  /// @return end_tick - start_tick.
  Tick durationTicks() const { return end_tick - start_tick; }

  /// @brief Get all notes from all entries, sorted by tick then voice.
  /// @return Merged and sorted note vector.
  std::vector<NoteEvent> allNotes() const;

  /// @brief Get the number of overlapping entries.
  /// @return Number of StrettoEntry objects.
  size_t entryCount() const { return entries.size(); }
};

/// @brief Generate a stretto section.
///
/// In a stretto, voices enter with the subject at shorter intervals than
/// the full subject length, creating dense overlapping imitation.
/// The stretto belongs to FuguePhase::Resolve and uses design values
/// directly (no search -- per design principle 4).
///
/// Design values:
///   - Entry interval: subject_length / num_voices (minimum 1 bar)
///   - Interval snapped to beat boundary
///   - All entries in home key
///   - Entry count matches num_voices
///   - Odd-indexed entries use melodic inversion for contrapuntal variety
///
/// @param subject The fugue subject.
/// @param home_key The home key (Resolve returns to tonic).
/// @param start_tick Starting tick for the stretto.
/// @param num_voices Number of voices (and stretto entries), clamped to [2, 5].
/// @param seed Random seed for deterministic generation.
/// @return Generated Stretto.
Stretto generateStretto(const Subject& subject, Key home_key, Tick start_tick,
                        uint8_t num_voices, uint32_t seed);

}  // namespace bach

#endif  // BACH_FUGUE_STRETTO_H
