// Obligation data structures for constraint-driven fugue generation.

#ifndef BACH_CONSTRAINT_OBLIGATION_H
#define BACH_CONSTRAINT_OBLIGATION_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

// ---------------------------------------------------------------------------
// Obligation type taxonomy
// ---------------------------------------------------------------------------

/// @brief Types of obligations extracted from a subject.
///
/// Obligations are "future resolution demands" that a subject's notes
/// generate. They fall into three categories:
/// - Debt: requires resolution within a deadline
/// - Gate: local filter (not counted as debt)
/// - InvariantRecovery: auto-generated in Phase 2 for soft invariant violations
enum class ObligationType : uint8_t {
  // --- Debt (resolution demands) ---
  LeadingTone,      ///< Leading tone -> half-step upward resolution
  Seventh,          ///< Seventh -> stepwise downward resolution
  LeapResolve,      ///< Leap (>4st) -> contrary stepwise motion
  CadenceStable,    ///< Cadence -> convergence to stable pitch
  CadenceApproach,  ///< Cadence approach -> soprano/bass approach pattern
                    ///< Section-final: Structural; internal: Soft.

  // --- Imitation obligations (from stretto.h/exposition.h structures) ---
  ImitationEntry,     ///< Voice must begin subject/answer at specific tick.
                      ///< Derived from VoiceEntry.entry_tick, MiddleEntry.start_tick.
  ImitationDistance,  ///< Minimum inter-subject distance in stretto.
                      ///< Derived from findValidStrettoIntervals() + stretto_matrix.

  // --- Gate (local filter, excluded from debt density) ---
  StrongBeatHarm,  ///< Strong beat -> harmonic tone gate

  // --- Invariant Recovery (Phase 2: auto-added on soft invariant violation) ---
  InvariantRecovery,  ///< Soft invariant violation (spacing, repetition, crossing)
                      ///< -> deadline-based recovery obligation.
                      ///< Expires on recovery; Gravity penalty on expiry.
};

/// @brief Convert ObligationType to string for debugging.
const char* obligationTypeToString(ObligationType type);

/// @brief Obligation strength levels.
enum class ObligationStrength : uint8_t {
  Structural,  ///< Mandatory (violation = structural failure)
  Soft,        ///< Recommended (violation = score penalty only)
};

/// @brief Convert ObligationStrength to string for debugging.
const char* obligationStrengthToString(ObligationStrength strength);

// ---------------------------------------------------------------------------
// Obligation node
// ---------------------------------------------------------------------------

/// @brief A single obligation extracted from subject analysis.
///
/// Represents a "future resolution demand" with temporal extent.
/// Phase 1: conflicts/satisfies not present; inter-obligation interaction
/// is approximated via synchronous_pressure.
struct ObligationNode {
  uint16_t id = 0;                           ///< Unique ID within profile
  ObligationType type = ObligationType::LeadingTone;
  Tick origin = 0;                           ///< Tick of the note that spawned this
  Tick start_tick = 0;                       ///< Earliest tick for resolution
  Tick deadline = 0;                         ///< Resolution deadline

  int8_t direction = 0;                      ///< +1=up, -1=down, 0=any
  uint8_t voice_mask = 0;                    ///< 0=unknown (unassigned in Phase 1)
  ObligationStrength strength = ObligationStrength::Structural;
  int8_t required_interval_semitones = 0;    ///< Resolution interval (LT=+1, 7th=-1)
  bool require_strong_beat = false;          ///< For StrongBeatHarm gate
  uint16_t require_pitch_class_mask = 0;     ///< Future use (0=unknown)

  /// IDs of obligations that cannot co-resolve (conflict graph).
  std::vector<uint16_t> conflicts;
  /// IDs of obligations that this obligation's resolution also satisfies.
  std::vector<uint16_t> satisfies;

  /// @brief Whether this obligation counts as debt (not a gate).
  bool is_debt() const {
    return type != ObligationType::StrongBeatHarm &&
           type != ObligationType::InvariantRecovery;
  }

  /// @brief Whether this obligation is active at a given tick.
  bool is_active_at(Tick tick) const {
    return tick >= start_tick && tick <= deadline;
  }
};

// ---------------------------------------------------------------------------
// Feasibility
// ---------------------------------------------------------------------------

/// @brief Solution space bounds for a constraint configuration.
struct Feasibility {
  uint16_t min_choices = 0;  ///< Lower bound on solution space size
  uint16_t max_choices = 0;  ///< Upper bound on solution space size
};

// ---------------------------------------------------------------------------
// Subject Constraint Profile
// ---------------------------------------------------------------------------

/// @brief Harmonic impulse: implied harmony from a melodic segment.
///
/// Extracted from pitch-class sets in the subject. Used in Gravity layer
/// (Phase 2) for directional guidance, NOT promoted to Obligation.
struct HarmonicImpulse {
  Tick tick = 0;
  uint8_t implied_degree = 0;      ///< 1-7 (scale degree)
  float strength = 0.0f;           ///< 0.0-1.0 (harmonic implication strength)
  int8_t directional_tendency = 0; ///< +1=dominant dir, -1=subdominant dir, 0=return
  float tension_level = 0.0f;      ///< 0.0-1.0: from computeHarmonicTension()
};

/// @brief Register trajectory: pitch envelope of the subject.
struct RegisterTrajectory {
  uint8_t opening_pitch = 0;      ///< Starting pitch (MIDI note)
  uint8_t peak_pitch = 0;         ///< Highest pitch (climax position)
  uint8_t closing_pitch = 0;      ///< Ending pitch (MIDI note)
  float peak_position = 0.0f;     ///< 0.0-1.0 (climax position ratio within subject)
  int8_t overall_direction = 0;   ///< +1=ascending, -1=descending, 0=return type
};

/// @brief Accent contour: strong-beat and long-note distribution pattern.
struct AccentContour {
  float front_weight = 0.0f;      ///< Accent density in first 1/3
  float mid_weight = 0.0f;        ///< Accent density in middle 1/3
  float tail_weight = 0.0f;       ///< Accent density in final 1/3
  float syncopation_ratio = 0.0f; ///< Syncopation ratio
};

/// @brief Stretto feasibility entry for a specific offset/voice configuration.
///
/// Evaluates both logical feasibility (obligation peaks) and musical
/// feasibility (counterpoint clashes, register overlap, perceptual clarity).
struct StrettoFeasibilityEntry {
  int offset_ticks = 0;       ///< Inter-subject onset distance
  int num_voices = 0;         ///< Number of simultaneous subject presentations
  float peak_obligation = 0.0f;  ///< Max simultaneous active debt count

  // Musical indices (beyond logical feasibility)
  float vertical_clash = 0.0f;           ///< P5/P8 parallel probability
  float rhythmic_interference = 0.0f;    ///< Simultaneous accent ratio
  float register_overlap = 0.0f;         ///< Pitch range intersection / union

  float perceptual_overlap_score = 0.0f;  ///< Subject Kerngestalt audibility
  float cadence_conflict_score = 0.0f;    ///< CadenceStable/Approach vs LT conflict

  /// @brief Combined feasibility score using geometric mean with floor guard.
  ///
  /// Stretto fails if ANY single dimension is poor ("weakest link dominance").
  /// Geometric mean ensures balanced high scores across all dimensions.
  /// Floor guard: any normalized dimension < 0.2 -> immediate infeasible.
  float feasibility_score() const {
    float norm_peak = 1.0f - std::min(peak_obligation / 4.0f, 1.0f);
    float norm_clash = 1.0f - vertical_clash;
    float norm_register = 1.0f - register_overlap;
    float norm_percept = 1.0f - perceptual_overlap_score;
    float norm_cadence = 1.0f - cadence_conflict_score;

    // Floor guard: any dimension < 0.2 -> infeasible
    float floor = std::min({norm_peak, norm_clash, norm_register,
                            norm_percept, norm_cadence});
    if (floor < 0.2f) return floor;

    // Geometric mean (5th root)
    float product = norm_peak * norm_clash * norm_register *
                    norm_percept * norm_cadence;
    return std::pow(product, 1.0f / 5.0f);
  }

  /// @brief Practical feasibility threshold (tuning target).
  static constexpr float kMinFeasibleScore = 0.5f;
};

/// @brief Complete constraint profile extracted from a subject.
///
/// Contains obligation nodes, density metrics, lateral dynamics
/// (harmonic impulse, register trajectory, accent contour),
/// and stretto feasibility matrix.
struct SubjectConstraintProfile {
  std::vector<ObligationNode> obligations;

  // --- Density metrics (StrongBeatHarm excluded from debt count) ---
  float peak_density = 0.0f;          ///< Max active debt at any tick
  float avg_density = 0.0f;           ///< Time-weighted average active debt
  float synchronous_pressure = 0.0f;  ///< Debt + StrongBeatHarm co-occurrence rate
  float voice_coupling = 0.0f;        ///< 0.0 in Phase 1 (single-voice analysis)

  // --- Imitation characteristics ---
  bool tonal_answer_feasible = false;
  bool invertible_8ve = false;

  float cadence_gravity = 0.0f;

  // --- Lateral dynamics (subject's four roles) ---
  std::vector<HarmonicImpulse> harmonic_impulses;
  RegisterTrajectory register_arc;
  AccentContour accent_contour;

  // --- Stretto feasibility matrix (computed in Phase 1e) ---
  std::vector<StrettoFeasibilityEntry> stretto_matrix;

  /// @brief Minimum safe stretto offset for a given voice count.
  /// @return Offset in ticks, or -1 if no feasible offset exists.
  int min_safe_stretto_offset(int num_voices) const;

  /// @brief Quick feasibility check based on density metrics.
  /// @param num_voices Number of fugue voices (3-5).
  bool feasible_for(int num_voices) const {
    return peak_density <= num_voices - 1 &&
           synchronous_pressure < 0.6f;
  }
};

}  // namespace bach

#endif  // BACH_CONSTRAINT_OBLIGATION_H
