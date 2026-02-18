// Fortspinnung ("spinning forth") episode generation using motif pool fragments.
//
// An additional episode variant for Baroque-style sequential development.
// Existing character-specific generators remain as the default; this engine
// provides an alternative that draws from the MotifPool for richer motivic
// continuity across episodes.

#ifndef BACH_FUGUE_FORTSPINNUNG_H
#define BACH_FUGUE_FORTSPINNUNG_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/motif_pool.h"
#include "fugue/subject.h"
#include "harmony/key.h"

namespace bach {

/// Exclusive episode generation mode. Selected once per episode to prevent
/// conflicting vocabulary systems from operating simultaneously.
enum class EpisodeVocabularyMode : uint8_t {
  Fortspinnung,     ///< Kernel->Sequence->Dissolution (motif pool based).
  VocabularyMotif,  ///< Existing tryVocabularyMotif path (figure matching).
  Plain,            ///< Standard episode (character-specific, no vocabulary).
};

/// Three-phase structure for Fortspinnung episodes.
///
/// Baroque Fortspinnung follows a Kernel->Sequence->Dissolution arc:
///   - Kernel: initial motivic statement (subject-derived, minimal transformation)
///   - Sequence: sequential development with transposition and variation
///   - Dissolution: fragmentation toward cadence (increasing stepwise motion,
///     decreasing density, lengthening final notes)
///
/// Calibrated from bach-reference episode internal arc analysis.
struct FortspinnungGrammar {
  float kernel_ratio = 0.25f;        ///< Fraction of episode for Kernel phase.
  float sequence_ratio = 0.50f;      ///< Fraction for Sequence phase.
  float dissolution_ratio = 0.25f;   ///< Fraction for Dissolution phase.

  // Dissolution phase characteristics.
  uint8_t min_fragment_notes = 2;    ///< Minimum notes in dissolution fragments.
  float stepwise_preference = 0.70f; ///< Scoring weight for stepwise motion (0.0-1.0).
  float density_decay_factor = 1.2f; ///< Inter-onset expansion per step (1.2 = 20% longer).
  float cadential_lengthening = 1.5f;  ///< Final 1-2 note duration multiplier.
};

/// Get the default FortspinnungGrammar for a subject character.
inline FortspinnungGrammar getFortspinnungGrammar(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {0.30f, 0.45f, 0.25f, 3, 0.65f, 1.15f, 1.5f};
    case SubjectCharacter::Playful:
      return {0.20f, 0.55f, 0.25f, 2, 0.60f, 1.25f, 1.3f};
    case SubjectCharacter::Noble:
      return {0.30f, 0.45f, 0.25f, 2, 0.75f, 1.20f, 1.6f};
    case SubjectCharacter::Restless:
      return {0.20f, 0.55f, 0.25f, 2, 0.55f, 1.30f, 1.4f};
    default:
      return {};
  }
}

/// @brief Three-phase classification within a Fortspinnung episode.
///
/// Baroque Fortspinnung follows a Kernel->Sequence->Dissolution arc.
/// Promoted from anonymous namespace in fortspinnung.cpp to enable
/// the constraint-driven episode generator (Phase 3) to reference phases.
enum class FortPhase : uint8_t { Kernel, Sequence, Dissolution };

/// @brief Planned step in a Fortspinnung episode arc.
///
/// planFortspinnung() returns a sequence of steps describing WHAT should
/// happen at each point in the episode, without placing actual notes.
/// The constraint episode generator uses these steps to place notes
/// with ConstraintState validation.
struct FortspinnungStep {
  Tick tick;                  ///< Absolute tick for this step.
  VoiceId voice;              ///< Target voice.
  MotifOp op;                 ///< Motif operation to apply.
  size_t pool_rank;           ///< Motif pool rank to use.
  FortPhase phase;            ///< Phase within the Fortspinnung arc.
  Tick suggested_duration;    ///< Suggested duration for the motif at this step.
};

/// @brief Plan the Fortspinnung arc without placing notes.
///
/// Returns a sequence of steps describing the motivic plan for each
/// point in the episode. The constraint episode generator consumes
/// this plan and places notes with ConstraintState validation.
///
/// Existing generateFortspinnung() is unchanged (backward compatible).
///
/// @param pool Motif pool for fragment selection.
/// @param grammar Phase ratios and dissolution parameters.
/// @param start_tick Episode start tick.
/// @param duration Episode duration in ticks.
/// @param num_voices Number of active voices.
/// @param character Subject character for weight selection.
/// @param seed RNG seed.
/// @return Planned steps sorted by tick.
std::vector<FortspinnungStep> planFortspinnung(const MotifPool& pool,
                                               const FortspinnungGrammar& grammar,
                                               Tick start_tick, Tick duration,
                                               uint8_t num_voices,
                                               SubjectCharacter character,
                                               uint32_t seed);

/// @brief Generate Fortspinnung-style episode material from the motif pool.
///
/// Fortspinnung ("spinning forth") is a Baroque compositional technique
/// where a short motif is developed through sequential progression,
/// fragmentation, and transformation. This engine uses the MotifPool
/// to select and connect fragments with proper voice-leading rules:
///   - Common tone connection (shared pitch between adjacent fragments)
///   - Stepwise connection (step of 1-2 semitones between fragments)
///   - Leap recovery (leaps > 4 semitones followed by contrary step)
///
/// Fortspinnung is an ADDITIONAL episode variant (not a replacement).
/// Existing character-specific generators remain as the default.
///
/// @param pool The motif pool (must be built before calling).
/// @param start_tick Starting tick position.
/// @param duration_ticks Available duration for the episode.
/// @param num_voices Number of active voices.
/// @param seed RNG seed for fragment selection.
/// @param character Subject character (influences fragment choice probability).
/// @param key Musical key for pedal anchor pitch calculation.
/// @return Vector of NoteEvents for the Fortspinnung passage.
std::vector<NoteEvent> generateFortspinnung(const MotifPool& pool,
                                            Tick start_tick,
                                            Tick duration_ticks,
                                            uint8_t num_voices,
                                            uint32_t seed,
                                            SubjectCharacter character,
                                            Key key);

}  // namespace bach

#endif  // BACH_FUGUE_FORTSPINNUNG_H
