// Middle entry: subject re-entry in a related key during the development.

#ifndef BACH_FUGUE_MIDDLE_ENTRY_H
#define BACH_FUGUE_MIDDLE_ENTRY_H

#include <vector>

#include "core/basic_types.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "fugue/subject.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

/// @brief A middle entry: subject re-statement in a related key.
///
/// Middle entries occur during FuguePhase::Develop and present the subject
/// transposed to a near-related key. They form the backbone of fugal
/// development between episodes.
struct MiddleEntry {
  std::vector<NoteEvent> notes;  ///< Transposed subject notes.
  Key key = Key::C;              ///< Key of this entry.
  Tick start_tick = 0;           ///< When this entry begins.
  Tick end_tick = 0;             ///< When this entry ends.
  VoiceId voice_id = 0;         ///< Which voice presents the subject.

  /// @brief Get duration in ticks.
  /// @return end_tick - start_tick.
  Tick durationTicks() const { return end_tick - start_tick; }
};

/// @brief Generate a middle entry by transposing the subject to a new key.
///
/// The subject is transposed to the target key, shifted to the target voice's
/// register, and placed at the given tick position. Middle entries belong to
/// FuguePhase::Develop.
///
/// @param subject The original fugue subject.
/// @param target_key The key to transpose to.
/// @param start_tick When this entry begins.
/// @param voice_id Which voice presents the entry.
/// @param num_voices Total number of voices (for register lookup).
/// @return Generated MiddleEntry.
MiddleEntry generateMiddleEntry(const Subject& subject, Key target_key, Tick start_tick,
                                VoiceId voice_id, uint8_t num_voices = 3,
                                uint8_t last_pitch = 0);

/// @brief Generate a middle entry with counterpoint validation.
///
/// Routes each note through createBachNote() with Immutable protection
/// (BachNoteSource::FugueSubject). Subject pitches are never altered;
/// notes that fail counterpoint validation are replaced by rests.
///
/// @param subject The original fugue subject.
/// @param target_key The key to transpose to.
/// @param start_tick When this entry begins.
/// @param voice_id Which voice presents the entry.
/// @param num_voices Total number of voices (for register lookup).
/// @param cp_state Counterpoint state for validation context.
/// @param cp_rules Rule evaluator for counterpoint checks.
/// @param cp_resolver Collision resolver.
/// @param timeline Harmonic timeline for chord-tone context.
/// @return Generated MiddleEntry with notes registered in cp_state.
MiddleEntry generateMiddleEntry(const Subject& subject, Key target_key, Tick start_tick,
                                VoiceId voice_id, uint8_t num_voices,
                                CounterpointState& cp_state, IRuleEvaluator& cp_rules,
                                CollisionResolver& cp_resolver,
                                const HarmonicTimeline& timeline,
                                uint8_t last_pitch = 0);

/// @brief Generate a false entry -- subject opening that diverges to free counterpoint.
///
/// A false entry presents the first 2-4 notes of the subject, creating the
/// expectation of a full entry, then departs into free counterpoint. This is
/// a hallmark of Bach's developmental writing, particularly in the Develop phase.
///
/// @param subject The subject to quote partially.
/// @param target_key Key for the false entry.
/// @param start_tick Starting tick position.
/// @param voice_id Voice to place the entry in.
/// @param num_voices Total number of voices (for register lookup).
/// @param quote_notes Number of subject notes to quote (2-4).
/// @return MiddleEntry containing the truncated subject + divergent tail.
MiddleEntry generateFalseEntry(const Subject& subject, Key target_key,
                               Tick start_tick, VoiceId voice_id,
                               uint8_t num_voices = 3, uint8_t quote_notes = 3);

}  // namespace bach

#endif  // BACH_FUGUE_MIDDLE_ENTRY_H
