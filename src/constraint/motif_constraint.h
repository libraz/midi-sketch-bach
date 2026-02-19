// Episode constraint types: MotifOp, CharacterEpisodeParams, EpisodeRequest/Result.

#ifndef BACH_CONSTRAINT_MOTIF_CONSTRAINT_H
#define BACH_CONSTRAINT_MOTIF_CONSTRAINT_H

#include <cstdint>
#include <vector>

#include "constraint/constraint_state.h"
#include "core/basic_types.h"
#include "fugue/fortspinnung.h"
#include "fugue/motif_pool.h"

namespace bach {

// Forward declarations
class CounterpointState;
class IRuleEvaluator;
class BachRuleEvaluator;
class CollisionResolver;
class HarmonicTimeline;

/// @brief Motif transformation operations for constraint-driven episodes.
enum class MotifOp : uint8_t {
  Original,    ///< Identity copy (no transformation).
  Invert,      ///< Diatonic inversion around first pitch.
  Retrograde,  ///< Reverse note order preserving rhythm.
  Diminish,    ///< Halve all durations.
  Augment,     ///< Double all durations.
  Fragment,    ///< Take first fragment (fragmentMotif()[0]).
  Sequence     ///< Diatonic sequential transposition.
};

/// @brief Convert MotifOp to string for debugging.
const char* motifOpToString(MotifOp op);

/// @brief Character-specific episode parameters.
///
/// Each SubjectCharacter maps to a fixed set of motif operations
/// and imitation parameters, following Bach's practice:
///   - Severe: original + inversion, wide imitation
///   - Playful: retrograde + inversion, tight imitation
///   - Noble: original + augmented retrograde, wide imitation
///   - Restless: fragment + diminution, tight imitation
struct CharacterEpisodeParams {
  MotifOp voice0_initial;      ///< Voice 0 initial transformation.
  MotifOp voice1_initial;      ///< Voice 1 initial transformation.
  MotifOp voice1_secondary;    ///< Secondary op for Noble (Retrograde after Augment).
                               ///< Set to Original for no secondary.
  float imitation_beats_lo;    ///< Imitation delay lower bound (beats).
  float imitation_beats_hi;    ///< Imitation delay upper bound (beats).
  int sequence_step;           ///< Sequence degree step (negative = descending).
};

/// @brief Get character-specific episode parameters.
/// @param character Subject character.
/// @return Constant parameter set (design values, not searched).
CharacterEpisodeParams getCharacterParams(SubjectCharacter character);

/// @brief Apply a MotifOp transformation to a note sequence.
///
/// Dispatches to existing transform functions:
///   Original  -> identity copy
///   Invert    -> invertMelodyDiatonic()
///   Retrograde -> retrogradeMelody()
///   Diminish  -> diminishMelody()
///   Augment   -> augmentMelody()
///   Fragment  -> fragmentMotif()[0]
///   Sequence  -> generateDiatonicSequence() (1 repetition)
///
/// @param notes Input note sequence (normalized to tick 0).
/// @param op Transformation to apply.
/// @param key Musical key (for diatonic operations).
/// @param scale Scale type (for diatonic operations).
/// @param sequence_step Degree step for Sequence op (ignored for others).
/// @return Transformed note sequence.
std::vector<NoteEvent> applyMotifOp(const std::vector<NoteEvent>& notes, MotifOp op,
                                    Key key = Key::C, ScaleType scale = ScaleType::Major,
                                    int sequence_step = -1);

/// @brief Request for constraint-driven episode generation.
struct EpisodeRequest {
  ConstraintState entry_state;   ///< Constraint state at episode start.
  Key start_key = Key::C;       ///< Key at episode start.
  Key end_key = Key::C;         ///< Target key at episode end.
  Tick start_tick = 0;           ///< Absolute tick of episode start.
  Tick duration = 0;             ///< Episode duration in ticks.
  uint8_t num_voices = 2;       ///< Number of active voices (1-5).
  const MotifPool* motif_pool = nullptr;  ///< Motif pool (read-only).
  SubjectCharacter character = SubjectCharacter::Severe;
  FortspinnungGrammar grammar;   ///< Fortspinnung phase ratios.
  int episode_index = 0;         ///< Episode ordinal (odd = invertible counterpoint).
  float energy_level = 0.5f;     ///< Energy level [0,1].
  uint32_t seed = 0;             ///< RNG seed.
  uint8_t pedal_pitch = 0;      ///< Active pedal pitch (0 = none).
  const IRuleEvaluator* rule_eval = nullptr;       ///< Rule evaluator for constraint evaluation.
  const BachRuleEvaluator* crossing_eval = nullptr; ///< Bach evaluator for crossing checks.
  const CounterpointState* cp_state_ctx = nullptr;  ///< Counterpoint state context.
  const SectionAccumulator* pipeline_accumulator = nullptr;  ///< Pipeline-level accumulator.
  const HarmonicTimeline* timeline = nullptr;  ///< Harmonic timeline for bass pitch selection.
  static constexpr int kMaxRequestVoices = 6;
  uint8_t last_pitches[kMaxRequestVoices] = {};  ///< Per-voice last pitch (0 = unknown).
};

/// @brief Result of constraint-driven episode generation.
struct EpisodeResult {
  std::vector<NoteEvent> notes;    ///< Generated episode notes.
  ConstraintState exit_state;      ///< Constraint state at episode end.
  Key achieved_key = Key::C;       ///< Actual key at episode end.
  bool success = false;            ///< False if deadlocked or failed.
};

}  // namespace bach

#endif  // BACH_CONSTRAINT_MOTIF_CONSTRAINT_H
