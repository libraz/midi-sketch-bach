// Phase 2: ConstraintState -- generation-time solvability tracking.
// Three-layer constraint model: Obligation + Invariant + Gravity.

#ifndef BACH_CONSTRAINT_CONSTRAINT_STATE_H
#define BACH_CONSTRAINT_CONSTRAINT_STATE_H

#include <array>
#include <cstdint>
#include <vector>

#include "constraint/obligation.h"
#include "core/basic_types.h"
#include "core/markov_tables.h"

namespace bach {

// Forward declarations
class CounterpointState;
class IRuleEvaluator;
class BachRuleEvaluator;
class CollisionResolver;

// ---------------------------------------------------------------------------
// VerticalSnapshot
// ---------------------------------------------------------------------------

/// @brief Lightweight snapshot of pitches sounding at a given tick.
struct VerticalSnapshot {
  static constexpr int kMaxVoices = 6;
  uint8_t pitches[kMaxVoices] = {};  ///< MIDI pitch per voice (0 = silent).
  uint8_t num_voices = 0;

  /// @brief Build from CounterpointState at a specific tick.
  static VerticalSnapshot fromState(const CounterpointState& state, Tick tick);
};

// ---------------------------------------------------------------------------
// MarkovContext
// ---------------------------------------------------------------------------

/// @brief Melodic context for Markov model scoring.
/// Packages all input dimensions needed by scoreMarkovPitch/Duration.
struct MarkovContext {
  DegreeStep prev_step = 0;
  DegreeClass deg_class = DegreeClass::Stable;
  BeatPos beat = BeatPos::Bar;
  DurCategory prev_dur = DurCategory::Qtr;
  DirIntervalClass dir_class = DirIntervalClass::StepUp;
  uint8_t prev_pitch = 60;
  Key key = Key::C;
  ScaleType scale = ScaleType::Major;
};

// ---------------------------------------------------------------------------
// CrossingPolicy
// ---------------------------------------------------------------------------

/// @brief Phase-dependent crossing tolerance.
enum class CrossingPolicy : uint8_t {
  AllowTemporary,  ///< Establish/Develop: temporary crossings permitted.
  Reject           ///< Resolve/Conclude: no crossings allowed.
};

// ---------------------------------------------------------------------------
// InvariantSet
// ---------------------------------------------------------------------------

/// @brief Hard/Soft invariant constraints checked per-note.
///
/// Hard invariants cause immediate rejection (candidate filtered out).
/// Soft invariants generate recovery obligations on violation.
struct InvariantSet {
  // --- Voice range (Hard) ---
  uint8_t voice_range_lo = 0;
  uint8_t voice_range_hi = 127;

  // --- Texture density target ---
  uint8_t min_active_voices = 1;
  uint8_t max_active_voices = 4;

  // --- Adjacent voice spacing (Soft, energy-dependent) ---
  int max_adjacent_spacing = 24;  ///< Semitones (base value from RegisterEnvelope).

  // --- Crossing policy (Phase-dependent) ---
  CrossingPolicy crossing_policy = CrossingPolicy::AllowTemporary;

  // --- Hard repeat limit ---
  uint8_t hard_repeat_limit = 4;  ///< Max consecutive same-pitch notes.

  /// @brief Check a candidate note against all invariants.
  /// @param pitch Candidate MIDI pitch.
  /// @param voice_id Voice placing the note.
  /// @param tick Tick position.
  /// @param snap Current vertical snapshot.
  /// @param rule_eval Rule evaluator for parallel perfect checks.
  /// @param crossing_eval Bach rule evaluator for crossing checks (nullable).
  /// @param state Counterpoint state for crossing temporality check.
  /// @param prev_pitches Array of previous pitches per voice (for repeat check).
  /// @param prev_pitch_count Count per voice (for repeat check).
  /// @return 0 if all pass, >0 for Hard violation count, <0 for Soft violations.
  ///         Hard violations = positive (reject candidate).
  ///         Soft violations = negative count (generate recovery obligations).
  struct SatisfiesResult {
    int hard_violations = 0;
    int soft_violations = 0;
    bool range_violation = false;
    bool parallel_perfect = false;
    bool crossing_violation = false;
    bool repeat_violation = false;
    bool spacing_violation = false;
  };

  SatisfiesResult satisfies(uint8_t pitch, VoiceId voice_id, Tick tick,
                            const VerticalSnapshot& snap,
                            const IRuleEvaluator* rule_eval,
                            const BachRuleEvaluator* crossing_eval,
                            const CounterpointState* state,
                            const uint8_t* recent_pitches,
                            int recent_count) const;
};

// ---------------------------------------------------------------------------
// SectionAccumulator
// ---------------------------------------------------------------------------

/// @brief Accumulates rhythm and harmony distributions within a section.
/// Computes JSD against reference distributions for Gravity guidance.
struct SectionAccumulator {
  static constexpr int kRhythmBins = 7;
  static constexpr int kHarmonyDegrees = 7;

  std::array<int, kRhythmBins> rhythm_counts = {};
  std::array<int, kHarmonyDegrees> harmony_counts = {};
  int total_rhythm = 0;
  int total_harmony = 0;
  FuguePhase current_phase = FuguePhase::Establish;

  /// @brief Record a note's rhythm and harmony contribution.
  /// @param duration Note duration in ticks.
  /// @param degree Scale degree (0-6).
  void recordNote(Tick duration, int degree);

  /// @brief Reset all accumulators.
  void reset();

  /// @brief Compute JSD between accumulated rhythm and reference.
  float rhythm_jsd() const;

  /// @brief Compute JSD between accumulated harmony and reference.
  float harmony_jsd() const;
};

// ---------------------------------------------------------------------------
// JSD computation
// ---------------------------------------------------------------------------

/// @brief Compute Jensen-Shannon Divergence between two distributions.
/// @param p First distribution (must sum to 1.0).
/// @param q Second distribution (must sum to 1.0).
/// @param n Number of bins.
/// @return JSD in [0, 1] (base-2 logarithm, so max = 1.0).
float computeJSD(const float* p, const float* q, int n);

/// @brief Normalize a uint16_t count array to a probability distribution.
/// @param counts Input counts.
/// @param out Output probabilities.
/// @param n Number of bins.
void normalizeDistribution(const uint16_t* counts, float* out, int n);

/// @brief Normalize an int count array to a probability distribution.
void normalizeDistribution(const int* counts, float* out, int n);

// ---------------------------------------------------------------------------
// GravityConfig
// ---------------------------------------------------------------------------

/// @brief Phase-specific weight configuration for Gravity scoring.
struct PhaseWeights {
  float melodic = 0.35f;    ///< Markov melodic weight.
  float vertical = 0.30f;   ///< Vertical interval weight.
  float rhythm = 0.20f;     ///< Rhythm distribution JSD weight.
  float vocabulary = 0.15f; ///< Figure vocabulary match weight.
};

/// @brief Get phase weights for a FuguePhase.
const PhaseWeights& getPhaseWeights(FuguePhase phase);

/// @brief JSD decay factor based on structural position.
/// Decays near cadences, at climax, and at phrase boundaries.
/// @param tick Current tick.
/// @param total_duration Total fugue duration.
/// @param cadence_ticks Sorted cadence positions.
/// @param energy Current energy level.
/// @return Factor in [0.3, 1.0] (lower = more lenient).
float jsd_decay_factor(Tick tick, Tick total_duration,
                       const std::vector<Tick>& cadence_ticks,
                       float energy);

/// @brief Gravity scoring configuration.
///
/// Combines Markov melodic/duration scores, vertical interval scores,
/// rhythm/harmony JSD penalties, and vocabulary attestation into a
/// single composite score.
struct GravityConfig {
  const MarkovModel* melodic_model = nullptr;
  const VerticalIntervalTable* vertical_table = nullptr;
  FuguePhase phase = FuguePhase::Establish;
  float energy = 0.5f;

  /// @brief Score a candidate note using the 4-layer Gravity model.
  ///
  /// Layers:
  ///   1. Melodic (Markov pitch + duration transition)
  ///   2. Vertical (interval probability with sounding voices)
  ///   3. Rhythm/Harmony JSD penalty (divergence from reference)
  ///   4. Vocabulary (figure attestation bonus)
  ///
  /// @param pitch Candidate MIDI pitch.
  /// @param duration Candidate duration.
  /// @param ctx Melodic context for Markov scoring.
  /// @param snap Current vertical snapshot.
  /// @param accum Section accumulator for JSD.
  /// @param decay JSD decay factor.
  /// @param figure_score Vocabulary figure match score [0,1].
  /// @return Composite score (higher = better).
  float score(uint8_t pitch, Tick duration,
              const MarkovContext& ctx,
              const VerticalSnapshot& snap,
              const SectionAccumulator& accum,
              float decay, float figure_score) const;
};

// ---------------------------------------------------------------------------
// ConstraintState
// ---------------------------------------------------------------------------

/// @brief Three-layer constraint state for generation-time solvability.
///
/// Layer 1: Obligation -- resolution demands with deadlines.
/// Layer 2: Invariant -- per-note hard/soft checks.
/// Layer 3: Gravity -- distribution guidance toward Bach reference.
///
/// The evaluate() method returns a composite feasibility signal.
/// The advance() method consumes/expires obligations as time progresses.
/// The is_dead() method detects irrecoverable states (min_choices == 0).
struct ConstraintState {
  // --- Layer 1: Obligations ---
  std::vector<ObligationNode> active_obligations;
  int soft_violation_count = 0;
  int total_note_count = 0;

  // --- Layer 2: Invariants ---
  InvariantSet invariants;

  // --- Layer 3: Gravity ---
  GravityConfig gravity;
  SectionAccumulator accumulator;

  // --- Configuration ---
  std::vector<Tick> cadence_ticks;
  Tick total_duration = 0;

  /// @brief Evaluate a candidate note against all three layers.
  ///
  /// @param pitch Candidate MIDI pitch.
  /// @param duration Candidate duration.
  /// @param voice_id Voice placing the note.
  /// @param tick Tick position.
  /// @param ctx Melodic context.
  /// @param snap Vertical snapshot.
  /// @param rule_eval Rule evaluator.
  /// @param crossing_eval Bach evaluator for crossings.
  /// @param cp_state Counterpoint state.
  /// @param recent_pitches Recent pitches for repeat check.
  /// @param recent_count Count of recent pitches.
  /// @param figure_score Vocabulary match score.
  /// @return Composite score. Negative infinity if Hard violated.
  float evaluate(uint8_t pitch, Tick duration, VoiceId voice_id, Tick tick,
                 const MarkovContext& ctx, const VerticalSnapshot& snap,
                 const IRuleEvaluator* rule_eval,
                 const BachRuleEvaluator* crossing_eval,
                 const CounterpointState* cp_state,
                 const uint8_t* recent_pitches, int recent_count,
                 float figure_score) const;

  /// @brief Advance time: consume resolved obligations, expire overdue ones.
  /// @param tick Current tick after note placement.
  /// @param placed_pitch Pitch that was placed.
  /// @param placed_voice Voice that received the note.
  void advance(Tick tick, uint8_t placed_pitch, VoiceId placed_voice,
               Tick duration = 0, Key key = Key::C);

  /// @brief Add a recovery obligation for a soft invariant violation.
  void addRecoveryObligation(ObligationType type, Tick origin, Tick deadline,
                             VoiceId voice);

  /// @brief Check if the fugue is in an irrecoverable state.
  /// @return True if Structural obligations cannot be met.
  bool is_dead() const;

  /// @brief Get current soft violation ratio.
  float soft_violation_ratio() const {
    return total_note_count > 0
               ? static_cast<float>(soft_violation_count) / total_note_count
               : 0.0f;
  }
};

}  // namespace bach

#endif  // BACH_CONSTRAINT_CONSTRAINT_STATE_H
