// Fugue episode: modulatory development of subject material.
//
// Episodes occur between subject entries in FuguePhase::Develop. They develop
// fragments (motifs) extracted from the subject using transformations such as
// sequence (Zeugma), inversion, augmentation, and diminution. No complete
// subject statement is permitted within an episode.

#ifndef BACH_FUGUE_EPISODE_H
#define BACH_FUGUE_EPISODE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/subject.h"

namespace bach {

// Forward declarations for counterpoint validation.
class CounterpointState;
class IRuleEvaluator;
class CollisionResolver;
class HarmonicTimeline;
class MotifPool;

/// @brief Episode material derived from subject fragments.
///
/// An episode fills the space between subject entries in a fugue's development
/// section. It modulates from one key toward another using sequential patterns
/// and motivic transformations of subject fragments.
struct Episode {
  std::vector<NoteEvent> notes;  ///< All episode notes (multi-voice).
  Key start_key = Key::C;       ///< Key at episode start.
  Key end_key = Key::C;         ///< Key at episode end (after modulation).
  Tick start_tick = 0;           ///< Absolute tick of episode start.
  Tick end_tick = 0;             ///< Absolute tick of episode end.

  /// @brief Get the duration of this episode in ticks.
  /// @return end_tick - start_tick.
  Tick durationTicks() const { return end_tick - start_tick; }

  /// @brief Get the number of notes in this episode.
  /// @return Note count.
  size_t noteCount() const { return notes.size(); }
};

/// @brief Generate an episode based on subject material.
///
/// Episodes develop fragments (motifs) extracted from the subject using
/// transformations: sequence (Zeugma), inversion, augmentation, diminution.
/// The episode modulates from start_key toward target_key.
///
/// Character influence on episode style (dialogic Kopfmotiv hand-off):
///   - Severe: original motif + direct Kopfmotiv imitation (2-beat delay)
///   - Playful: retrograde motif + diatonic-inverted Kopfmotiv (1-beat delay)
///   - Noble: original motif + augmented Kopfmotiv bass (2-beat delay)
///   - Restless: fragmented motif + diminished Kopfmotiv (1-beat delay)
///
/// Invertible counterpoint: when episode_index is odd, voice 0 and voice 1
/// material is swapped, placing upper material in the lower voice and vice
/// versa (double counterpoint at the octave).
///
/// @param subject The fugue subject (source material).
/// @param start_tick Starting tick position.
/// @param duration_ticks Length of the episode in ticks.
/// @param start_key Key at episode start.
/// @param target_key Key to modulate toward.
/// @param num_voices Number of active voices (1-5).
/// @param seed Random seed for deterministic generation.
/// @param episode_index Episode ordinal within the fugue (0-based). Odd indices
///        trigger invertible counterpoint (voice swap).
/// @param energy_level Energy level in [0,1] for rhythm density control (default 0.5).
///        Higher energy allows shorter note durations via FugueEnergyCurve::minDuration().
/// @return Generated Episode.
Episode generateEpisode(const Subject& subject, Tick start_tick, Tick duration_ticks,
                        Key start_key, Key target_key, uint8_t num_voices, uint32_t seed,
                        int episode_index = 0, float energy_level = 0.5f);

/// @brief Generate an episode with harmonic validation.
///
/// Generates episode material using the character-specific generator, then
/// post-validates each note through createBachNote() with chord-tone snapping.
/// Non-chord tones on strong beats are snapped to the nearest chord tone.
/// Notes that fail counterpoint validation become rests.
///
/// @param subject The fugue subject (source material).
/// @param start_tick Starting tick position.
/// @param duration_ticks Length of the episode in ticks.
/// @param start_key Key at episode start.
/// @param target_key Key to modulate toward.
/// @param num_voices Number of active voices (1-5).
/// @param seed Random seed for deterministic generation.
/// @param episode_index Episode ordinal within the fugue (0-based).
/// @param energy_level Energy level in [0,1] for rhythm density control.
/// @param cp_state Counterpoint state for validation (notes added in-place).
/// @param cp_rules Rule evaluator for counterpoint checking.
/// @param cp_resolver Collision resolver for pitch adjustment.
/// @param timeline Harmonic timeline for chord-tone context.
/// @return Generated Episode with validated notes.
Episode generateEpisode(const Subject& subject, Tick start_tick, Tick duration_ticks,
                        Key start_key, Key target_key, uint8_t num_voices, uint32_t seed,
                        int episode_index, float energy_level,
                        CounterpointState& cp_state, IRuleEvaluator& cp_rules,
                        CollisionResolver& cp_resolver, const HarmonicTimeline& timeline);

/// @brief Generate a Fortspinnung-style episode using motif pool fragments.
///
/// Wraps generateFortspinnung() into an Episode struct with proper key
/// modulation, energy-based rhythm density, and invertible counterpoint
/// for odd-indexed episodes. Falls back to standard character-specific
/// generation (generateEpisode) if the pool is empty.
///
/// @param subject The fugue subject (for fallback and character context).
/// @param pool The motif pool (read-only, must be built before calling).
/// @param start_tick Starting tick position.
/// @param duration_ticks Length of the episode in ticks.
/// @param start_key Key at episode start.
/// @param target_key Key to modulate toward.
/// @param num_voices Number of active voices (1-5).
/// @param seed Random seed for deterministic generation.
/// @param episode_index Episode ordinal within the fugue (0-based). Odd indices
///        trigger invertible counterpoint (voice swap).
/// @param energy_level Energy level in [0,1] for rhythm density control.
/// @return Generated Episode.
Episode generateFortspinnungEpisode(const Subject& subject, const MotifPool& pool,
                                    Tick start_tick, Tick duration_ticks,
                                    Key start_key, Key target_key,
                                    uint8_t num_voices, uint32_t seed,
                                    int episode_index, float energy_level);

/// @brief Extract a motif (fragment) from the beginning of the subject.
///
/// Extracts the first N notes from the subject for use as episode material.
/// The returned notes retain their original timing from the subject.
///
/// @param subject Source subject.
/// @param max_notes Maximum number of notes to extract (default: 4).
/// @return Vector of notes forming the motif, empty if subject is empty.
std::vector<NoteEvent> extractMotif(const Subject& subject, size_t max_notes = 4);

/// @brief Extract the most characteristic motif from the subject.
///
/// Instead of always taking the first N notes, scores each 4-note window
/// of the subject by rhythmic diversity, intervallic interest, and
/// proximity to the subject opening. Returns the highest-scoring window.
///
/// Scoring criteria:
///   +0.3  Rhythmic diversity (distinct durations / window_size)
///   +0.3  Intervallic interest (contains a leap >= 3 semitones)
///   +0.2  Proximity to subject opening (front-weighted)
///   +0.2  Tonal stability (contains root or fifth pitch class)
///
/// @param subject Source subject.
/// @param motif_length Number of notes in the extracted motif (default: 4).
/// @return Vector of notes forming the characteristic motif.
std::vector<NoteEvent> extractCharacteristicMotif(const Subject& subject,
                                                   size_t motif_length = 4);

/// @brief Extract the tail portion of a melody (last N notes).
/// @param notes Source melody notes.
/// @param num_notes Number of notes from the tail to extract.
/// @return Vector of the last min(num_notes, notes.size()) notes.
std::vector<NoteEvent> extractTailMotif(const std::vector<NoteEvent>& notes, size_t num_notes);

/// @brief Fragment a melody into N equal-length segments.
/// @param notes Source melody notes.
/// @param num_fragments Number of fragments to create.
/// @return Vector of fragment vectors. The last fragment may be shorter if
///         the note count is not evenly divisible.
std::vector<std::vector<NoteEvent>> fragmentMotif(const std::vector<NoteEvent>& notes,
                                                   size_t num_fragments);

}  // namespace bach

#endif  // BACH_FUGUE_EPISODE_H
