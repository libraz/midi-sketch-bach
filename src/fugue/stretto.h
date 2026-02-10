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
///   - First entry interval: subject_length / num_voices (minimum 1 bar)
///   - Each subsequent interval: 75% of the previous (progressive shortening)
///   - All entries in home key
///   - Entry count matches num_voices
///   - Odd-indexed entries use character-specific transforms
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

/// @brief Find valid stretto entry intervals where overlapping subject entries are consonant.
///
/// Tests each beat-aligned offset from 1 beat up to max_offset. At each offset, checks
/// whether all simultaneously sounding notes between the original and delayed entry form
/// consonant intervals (unison, minor/major 3rd, perfect 5th, minor/major 6th, octave).
///
/// @param subject_notes The subject's notes.
/// @param max_offset Maximum offset to test (in ticks).
/// @return Vector of valid offsets (in ticks) sorted from shortest to longest.
std::vector<Tick> findValidStrettoIntervals(const std::vector<NoteEvent>& subject_notes,
                                            Tick max_offset);

/// @brief Generate a stretto section.
///
/// In a stretto, voices enter with the subject at progressively shorter
/// intervals, creating increasingly dense overlapping imitation.
/// The stretto belongs to FuguePhase::Resolve and uses design values
/// directly (no search -- per design principle 4).
///
/// Design values:
///   - First entry interval: subject_length / num_voices (minimum 1 bar)
///   - Each subsequent interval: 75% of previous (minimum 1 bar, beat-snapped)
///   - When valid consonant intervals exist, prefer those over raw calculation
///   - All entries in home key
///   - Entry count matches num_voices
///   - Odd-indexed entries use character-specific transforms:
///     Severe/Restless = inversion, Playful = retrograde, Noble = augmentation
///
/// @param subject The fugue subject.
/// @param home_key The home key (Resolve returns to tonic).
/// @param start_tick Starting tick for the stretto.
/// @param num_voices Number of voices (and stretto entries), clamped to [2, 5].
/// @param seed Random seed for deterministic generation.
/// @param character Subject character controlling odd-entry transform selection.
/// @return Generated Stretto.
Stretto generateStretto(const Subject& subject, Key home_key, Tick start_tick,
                        uint8_t num_voices, uint32_t seed,
                        SubjectCharacter character = SubjectCharacter::Severe);

/// @brief Create a stretto fragment from the subject head.
///
/// Extracts a portion of the subject for use in a fragmentary stretto
/// entry, where only the opening motif is presented before the next
/// voice enters. This intensifies the stretto effect.
///
/// @param subject The complete fugue subject.
/// @param fragment_ratio Fraction of the subject to use (0.0-1.0, default 0.5).
/// @return Vector of notes representing the subject fragment.
std::vector<NoteEvent> createStrettoFragment(const Subject& subject,
                                              float fragment_ratio = 0.5f);

}  // namespace bach

#endif  // BACH_FUGUE_STRETTO_H
