// Fugue exposition: initial presentation of subject and answer in all voices.
//
// The exposition is the first section of a fugue (FuguePhase::Establish),
// where each voice enters in succession with the subject or answer while
// previously entered voices continue with countersubject or free counterpoint.

#ifndef BACH_FUGUE_EXPOSITION_H
#define BACH_FUGUE_EXPOSITION_H

#include <cstdint>
#include <map>
#include <vector>

#include "core/basic_types.h"
#include "fugue/answer.h"
#include "fugue/countersubject.h"
#include "fugue/fugue_config.h"
#include "fugue/subject.h"

namespace bach {

// Forward declarations for counterpoint validation.
class CounterpointState;
class IRuleEvaluator;
class CollisionResolver;
class HarmonicTimeline;

/// @brief Entry order for a single voice in the exposition.
///
/// Each VoiceEntry records when and how a voice enters the fugue. Voices
/// alternate between presenting the subject and the answer. The entry order
/// and roles are fixed at construction and never changed (immutable by design).
struct VoiceEntry {
  VoiceId voice_id = 0;                    ///< Which voice enters.
  VoiceRole role = VoiceRole::Assert;      ///< Structural role (immutable).
  Tick entry_tick = 0;                     ///< Absolute tick when voice enters.
  bool is_subject = true;                  ///< true = subject, false = answer.
  uint8_t entry_number = 0;               ///< 1-based entry number in exposition.
};

/// @brief Complete exposition structure with voice entries and generated notes.
///
/// The exposition contains the plan (entries) and the generated musical
/// material (voice_notes). It belongs to FuguePhase::Establish and must be
/// the first section in the fugue structure.
struct Exposition {
  std::vector<VoiceEntry> entries;         ///< Ordered voice entry plan.
  Tick total_ticks = 0;                    ///< Total duration of the exposition.

  /// All generated notes organized by voice.
  std::map<VoiceId, std::vector<NoteEvent>> voice_notes;

  /// @brief Get the number of voice entries.
  /// @return Number of entries in the exposition.
  size_t entryCount() const { return entries.size(); }

  /// @brief Get all notes flattened into a single vector.
  ///
  /// Collects notes from all voices and sorts them by start_tick. Within
  /// the same tick, notes are ordered by voice_id (lower voice first).
  ///
  /// @return All notes from all voices, sorted by start_tick then voice.
  std::vector<NoteEvent> allNotes() const;
};

/// @brief Build an exposition for a fugue.
///
/// The exposition presents the subject in each voice in alternation:
///   - 3 voices: Subject (voice 0) -> Answer (voice 1) -> Subject (voice 2)
///   - 4 voices: Subject -> Answer -> Subject -> Answer
///   - 5 voices: Subject -> Answer -> Subject -> Answer -> Subject
///
/// Each new voice entry begins after the previous voice's subject/answer
/// completes. While a new voice presents the subject/answer, previously
/// entered voices continue with countersubject material. The voice that
/// most recently completed its subject/answer plays the countersubject;
/// earlier voices rest or sustain (free counterpoint placeholder).
///
/// Voice role assignment (immutable):
///   - Voice 0: Assert (first subject entry)
///   - Voice 1: Respond (first answer entry)
///   - Voice 2: Propel (drives counterpoint)
///   - Voice 3+: Ground (bass foundation)
///
/// @param subject The fugue subject (dux).
/// @param answer The fugue answer (comes), pre-generated.
/// @param countersubject The countersubject, pre-generated.
/// @param config Fugue configuration (num_voices, key, etc.).
/// @param seed Random seed for deterministic free counterpoint generation.
/// @return Complete Exposition with voice entries and notes.
Exposition buildExposition(const Subject& subject,
                           const Answer& answer,
                           const Countersubject& countersubject,
                           const FugueConfig& config,
                           uint32_t seed);

/// @brief Build an exposition with counterpoint validation.
///
/// Same as the unvalidated overload, but free counterpoint notes are
/// routed through createBachNote() for counterpoint rule checking.
/// Subject and answer notes are registered in the counterpoint state
/// but not altered (immutable sources).
///
/// @param subject The fugue subject (dux).
/// @param answer The fugue answer (comes), pre-generated.
/// @param countersubject The countersubject, pre-generated.
/// @param config Fugue configuration.
/// @param seed Random seed.
/// @param cp_state Counterpoint state for validation.
/// @param cp_rules Rule evaluator.
/// @param cp_resolver Collision resolver.
/// @param timeline Harmonic timeline for chord-tone context.
/// @return Complete Exposition with validated free counterpoint.
Exposition buildExposition(const Subject& subject,
                           const Answer& answer,
                           const Countersubject& countersubject,
                           const FugueConfig& config,
                           uint32_t seed,
                           CounterpointState& cp_state,
                           IRuleEvaluator& cp_rules,
                           CollisionResolver& cp_resolver,
                           const HarmonicTimeline& timeline);

}  // namespace bach

#endif  // BACH_FUGUE_EXPOSITION_H
