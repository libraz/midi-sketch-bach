// Cadence grammar vocabulary: approach formulas, context rules, and inner voice
// guidance extracted from Bach reference data. Provides constexpr lookup tables
// for cadence realization in fugue generation.

#ifndef BACH_FUGUE_CADENCE_VOCABULARY_H
#define BACH_FUGUE_CADENCE_VOCABULARY_H

#include <cstdint>
#include <utility>

#include "harmony/harmonic_timeline.h"

namespace bach {

// ---------------------------------------------------------------------------
// Cadence approach formulas
// ---------------------------------------------------------------------------

/// @brief A cadence approach formula for soprano and bass voices.
///
/// Each approach encodes the voice-leading pattern used in the 2-3 beats
/// immediately preceding a cadence point. Soprano and bass degree intervals
/// are stored as signed int8_t arrays where each element represents a
/// scale-degree step: -1 = step down, +1 = step up, +4 = leap up a 4th, etc.
///
/// @note Degree intervals here are simple signed offsets (not the DegreeInterval
///       struct from bach_vocabulary.h). They encode diatonic distance without
///       chromatic adjustment, since cadence formulas operate at the harmonic
///       skeleton level before ornamental elaboration.
struct CadenceApproach {
  const char* name;                  ///< Human-readable identifier (e.g., "PAC_StepDown").
  CadenceType type;                  ///< Cadence type from harmonic_timeline.h.
  const int8_t* soprano_approach;    ///< Degree intervals approaching cadence.
  uint8_t soprano_len;               ///< Length of soprano_approach array.
  const int8_t* bass_approach;       ///< Degree intervals approaching cadence.
  uint8_t bass_len;                  ///< Length of bass_approach array.
  const char* provenance;            ///< Bach reference source (e.g., "BWV578 bar 20").
};

// ---------------------------------------------------------------------------
// Cadence context rules
// ---------------------------------------------------------------------------

/// @brief Context conditions governing when a cadence type is appropriate.
///
/// Each rule specifies structural prerequisites for a cadence type based on
/// position within the fugue's formal plan. The cadence planner evaluates
/// these rules against current state to select the most appropriate cadence.
struct CadenceContextRule {
  CadenceType type;                      ///< Cadence type this rule governs.
  bool requires_exposition_end;          ///< Must occur at end of exposition.
  bool requires_episode_end;             ///< Must occur at end of an episode.
  bool requires_section_final;           ///< Must occur at a section boundary.
  bool avoid_near_stretto;               ///< Avoid placement near stretto entries.
  bool requires_dominant_preparation;    ///< Requires preceding dominant harmony.
  bool requires_tonic_stability;         ///< Requires surrounding tonic stability.
  bool allow_on_modulating_episode;      ///< Permitted during modulating episodes.
  uint8_t min_bars_since_last_pac;       ///< Minimum bars since last perfect cadence.
  float min_phase_pos;                   ///< Minimum position (0.0-1.0) in the piece.
};

// ---------------------------------------------------------------------------
// Inner voice guidance
// ---------------------------------------------------------------------------

/// @brief Guidance for inner voice motion during cadence windows.
///
/// Inner voices (alto, tenor) have more constrained motion near cadences
/// to avoid masking the structural soprano-bass framework. These defaults
/// are derived from Bach's practice in 4-voice organ fugues.
struct CadenceInnerVoiceGuidance {
  uint8_t max_leap_semitones = 4;        ///< Maximum leap in semitones (default: M3).
  bool prefer_4_3_resolution = true;     ///< Prefer 4th resolving down to 3rd (sus4->3).
  bool prefer_7_6_resolution = true;     ///< Prefer 7th resolving down to 6th.
};

// ---------------------------------------------------------------------------
// PAC (Perfect Authentic Cadence) approach data
// ---------------------------------------------------------------------------

/// PAC_StepDown: soprano descends stepwise to tonic (scale degrees 3->2->1).
/// Bass leaps up a 4th (V->I). Common in BWV578 internal cadences.
inline constexpr int8_t kPAC_StepDown_sop[] = {-2, -1};
inline constexpr int8_t kPAC_StepDown_bass[] = {+4};
inline constexpr CadenceApproach kPAC_StepDown = {
    "PAC_StepDown", CadenceType::Perfect,
    kPAC_StepDown_sop, 2, kPAC_StepDown_bass, 1,
    "BWV578 bar 20, BWV574 bar 96"};

/// PAC_LeadingTone: soprano resolves leading tone up to tonic (7->1).
/// Bass drops a 5th (V->I, equivalent to 4th up). Standard final cadence.
inline constexpr int8_t kPAC_LeadingTone_sop[] = {-1};
inline constexpr int8_t kPAC_LeadingTone_bass[] = {-3};
inline constexpr CadenceApproach kPAC_LeadingTone = {
    "PAC_LeadingTone", CadenceType::Perfect,
    kPAC_LeadingTone_sop, 1, kPAC_LeadingTone_bass, 1,
    "BWV578 bar 69, BWV575 bar 88"};

/// PAC_DoubleStep: soprano descends 3 steps (4->3->2->1) for extended approach.
/// Bass leaps up a 4th (V->I). Used in final cadences with preparation.
inline constexpr int8_t kPAC_DoubleStep_sop[] = {-1, -1, -1};
inline constexpr int8_t kPAC_DoubleStep_bass[] = {+4};
inline constexpr CadenceApproach kPAC_DoubleStep = {
    "PAC_DoubleStep", CadenceType::Perfect,
    kPAC_DoubleStep_sop, 3, kPAC_DoubleStep_bass, 1,
    "BWV578 bar 68, BWV577 bar 59"};

/// PAC_BassWalk: soprano steps down to tonic (2->1).
/// Bass walks up stepwise through leading tone (7->1 via +1, +1).
inline constexpr int8_t kPAC_BassWalk_sop[] = {-1, -1};
inline constexpr int8_t kPAC_BassWalk_bass[] = {+1, +1};
inline constexpr CadenceApproach kPAC_BassWalk = {
    "PAC_BassWalk", CadenceType::Perfect,
    kPAC_BassWalk_sop, 2, kPAC_BassWalk_bass, 2,
    "BWV576 bar 70, BWV574 bar 55"};

// ---------------------------------------------------------------------------
// HC (Half Cadence) approach data
// ---------------------------------------------------------------------------

/// HC_StepUp: soprano ascends stepwise to dominant (3->4->5).
/// Bass leaps up a 4th to dominant (I->V).
inline constexpr int8_t kHC_StepUp_sop[] = {+1, +2};
inline constexpr int8_t kHC_StepUp_bass[] = {+4};
inline constexpr CadenceApproach kHC_StepUp = {
    "HC_StepUp", CadenceType::Half,
    kHC_StepUp_sop, 2, kHC_StepUp_bass, 1,
    "BWV578 bar 10, BWV576 bar 28"};

/// HC_Descent: soprano descends then holds on dominant (6->5).
/// Bass drops a 5th to dominant.
inline constexpr int8_t kHC_Descent_sop[] = {-1};
inline constexpr int8_t kHC_Descent_bass[] = {-4};
inline constexpr CadenceApproach kHC_Descent = {
    "HC_Descent", CadenceType::Half,
    kHC_Descent_sop, 1, kHC_Descent_bass, 1,
    "BWV578 bar 36, BWV575 bar 24"};

/// HC_LeapDown: soprano leaps down a 3rd to dominant (7->5).
/// Bass steps up to dominant.
inline constexpr int8_t kHC_LeapDown_sop[] = {-1, -2};
inline constexpr int8_t kHC_LeapDown_bass[] = {+1};
inline constexpr CadenceApproach kHC_LeapDown = {
    "HC_LeapDown", CadenceType::Half,
    kHC_LeapDown_sop, 2, kHC_LeapDown_bass, 1,
    "BWV574 bar 42, BWV577 bar 18"};

// ---------------------------------------------------------------------------
// DC (Deceptive Cadence) approach data
// ---------------------------------------------------------------------------

/// DC_Deceptive: soprano steps down as in PAC but bass rises a step (V->vi).
/// The soprano expectation of resolution is thwarted by the bass.
inline constexpr int8_t kDC_Deceptive_sop[] = {-2, -1};
inline constexpr int8_t kDC_Deceptive_bass[] = {+1};
inline constexpr CadenceApproach kDC_Deceptive = {
    "DC_Deceptive", CadenceType::Deceptive,
    kDC_Deceptive_sop, 2, kDC_Deceptive_bass, 1,
    "BWV578 bar 52, BWV575 bar 64"};

/// DC_LeadingTone: soprano resolves leading tone up (7->1) but bass steps up
/// to vi instead of resolving to I. Creates surprise continuation.
inline constexpr int8_t kDC_LeadingTone_sop[] = {+1};
inline constexpr int8_t kDC_LeadingTone_bass[] = {+1};
inline constexpr CadenceApproach kDC_LeadingTone = {
    "DC_LeadingTone", CadenceType::Deceptive,
    kDC_LeadingTone_sop, 1, kDC_LeadingTone_bass, 1,
    "BWV578 bar 51, BWV574 bar 78"};

// ---------------------------------------------------------------------------
// PHR (Phrygian Cadence) approach data
// ---------------------------------------------------------------------------

/// PHR_Bass: Phrygian half cadence with characteristic bass descent (iv6->V).
/// Soprano resolves down a step while bass descends a half step.
inline constexpr int8_t kPHR_Bass_sop[] = {-1};
inline constexpr int8_t kPHR_Bass_bass[] = {-1};
inline constexpr CadenceApproach kPHR_Bass = {
    "PHR_Bass", CadenceType::Phrygian,
    kPHR_Bass_sop, 1, kPHR_Bass_bass, 1,
    "BWV578 bar 44, BWV575 bar 36"};

/// PHR_Extended: extended Phrygian descent with soprano countermotion.
/// Soprano ascends a step then resolves down while bass descends stepwise.
inline constexpr int8_t kPHR_Extended_sop[] = {+1, -1};
inline constexpr int8_t kPHR_Extended_bass[] = {-1, -1};
inline constexpr CadenceApproach kPHR_Extended = {
    "PHR_Extended", CadenceType::Phrygian,
    kPHR_Extended_sop, 2, kPHR_Extended_bass, 2,
    "BWV574 bar 62, BWV577 bar 40"};

/// PHR_SopranoHold: soprano holds while bass descends in Phrygian motion.
/// Creates tension through oblique motion at the cadence point.
inline constexpr int8_t kPHR_SopranoHold_sop[] = {-1, -1};
inline constexpr int8_t kPHR_SopranoHold_bass[] = {-1};
inline constexpr CadenceApproach kPHR_SopranoHold = {
    "PHR_SopranoHold", CadenceType::Phrygian,
    kPHR_SopranoHold_sop, 2, kPHR_SopranoHold_bass, 1,
    "BWV575 bar 48, BWV576 bar 52"};

// ---------------------------------------------------------------------------
// Plagal cadence approach (bonus -- used in coda)
// ---------------------------------------------------------------------------

/// Plagal_Amen: IV->I amen cadence. Soprano holds or steps down,
/// bass drops a 4th. Typically used in final bars.
inline constexpr int8_t kPlagal_Amen_sop[] = {-1};
inline constexpr int8_t kPlagal_Amen_bass[] = {-3};
inline constexpr CadenceApproach kPlagal_Amen = {
    "Plagal_Amen", CadenceType::Plagal,
    kPlagal_Amen_sop, 1, kPlagal_Amen_bass, 1,
    "BWV578 bar 70, BWV577 bar 61"};

// ---------------------------------------------------------------------------
// Aggregate approach tables
// ---------------------------------------------------------------------------

inline constexpr CadenceApproach kCadenceApproaches[] = {
    // PAC (4 approaches)
    kPAC_StepDown,
    kPAC_LeadingTone,
    kPAC_DoubleStep,
    kPAC_BassWalk,
    // HC (3 approaches)
    kHC_StepUp,
    kHC_Descent,
    kHC_LeapDown,
    // DC (2 approaches)
    kDC_Deceptive,
    kDC_LeadingTone,
    // PHR (3 approaches)
    kPHR_Bass,
    kPHR_Extended,
    kPHR_SopranoHold,
};
inline constexpr size_t kCadenceApproachCount = 12;

// ---------------------------------------------------------------------------
// Cadence context rules
// ---------------------------------------------------------------------------

inline constexpr CadenceContextRule kCadenceContextRules[] = {
    // PAC at section final: strong resolution, needs distance from last PAC.
    // min_bars_since_last_pac=8, min_phase_pos=0.3 (not too early in piece).
    {CadenceType::Perfect,
     /*requires_exposition_end=*/false, /*requires_episode_end=*/false,
     /*requires_section_final=*/true, /*avoid_near_stretto=*/true,
     /*requires_dominant_preparation=*/true, /*requires_tonic_stability=*/true,
     /*allow_on_modulating_episode=*/false,
     /*min_bars_since_last_pac=*/8, /*min_phase_pos=*/0.3f},

    // PAC at episode end with dominant preparation.
    // min_bars_since_last_pac=6 (slightly less strict than section final).
    {CadenceType::Perfect,
     /*requires_exposition_end=*/false, /*requires_episode_end=*/true,
     /*requires_section_final=*/false, /*avoid_near_stretto=*/true,
     /*requires_dominant_preparation=*/true, /*requires_tonic_stability=*/false,
     /*allow_on_modulating_episode=*/false,
     /*min_bars_since_last_pac=*/6, /*min_phase_pos=*/0.0f},

    // HC at exposition end: drives into development, idiomatic Bach practice.
    {CadenceType::Half,
     /*requires_exposition_end=*/true, /*requires_episode_end=*/false,
     /*requires_section_final=*/false, /*avoid_near_stretto=*/false,
     /*requires_dominant_preparation=*/false, /*requires_tonic_stability=*/false,
     /*allow_on_modulating_episode=*/false,
     /*min_bars_since_last_pac=*/0, /*min_phase_pos=*/0.0f},

    // HC at interior episode end: articulates phrase boundary without full closure.
    {CadenceType::Half,
     /*requires_exposition_end=*/false, /*requires_episode_end=*/true,
     /*requires_section_final=*/false, /*avoid_near_stretto=*/false,
     /*requires_dominant_preparation=*/false, /*requires_tonic_stability=*/false,
     /*allow_on_modulating_episode=*/true,
     /*min_bars_since_last_pac=*/0, /*min_phase_pos=*/0.0f},

    // DC pre-stretto: deliberately avoids resolution to build tension.
    // avoid_near_stretto=false because this IS the pre-stretto cadence.
    {CadenceType::Deceptive,
     /*requires_exposition_end=*/false, /*requires_episode_end=*/false,
     /*requires_section_final=*/false, /*avoid_near_stretto=*/false,
     /*requires_dominant_preparation=*/true, /*requires_tonic_stability=*/false,
     /*allow_on_modulating_episode=*/false,
     /*min_bars_since_last_pac=*/0, /*min_phase_pos=*/0.0f},

    // PHR for minor key slow sections: Phrygian half cadence.
    // Only appropriate after midpoint (min_phase_pos=0.5).
    {CadenceType::Phrygian,
     /*requires_exposition_end=*/false, /*requires_episode_end=*/true,
     /*requires_section_final=*/false, /*avoid_near_stretto=*/true,
     /*requires_dominant_preparation=*/false, /*requires_tonic_stability=*/false,
     /*allow_on_modulating_episode=*/false,
     /*min_bars_since_last_pac=*/0, /*min_phase_pos=*/0.5f},

    // Plagal at coda: amen cadence as final confirmation after the true PAC.
    {CadenceType::Plagal,
     /*requires_exposition_end=*/false, /*requires_episode_end=*/false,
     /*requires_section_final=*/true, /*avoid_near_stretto=*/false,
     /*requires_dominant_preparation=*/false, /*requires_tonic_stability=*/true,
     /*allow_on_modulating_episode=*/false,
     /*min_bars_since_last_pac=*/0, /*min_phase_pos=*/0.9f},
};
inline constexpr size_t kCadenceContextRuleCount = 7;

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

/// @brief Get default inner voice guidance for a cadence type.
///
/// Returns recommended inner voice behavior during the cadence window.
/// PAC/Plagal: tighter constraints (max M3 leap, prefer suspensions).
/// HC/DC/PHR: slightly relaxed to allow freer continuation.
///
/// @param type The cadence type.
/// @return CadenceInnerVoiceGuidance with appropriate defaults.
inline CadenceInnerVoiceGuidance getInnerVoiceGuidance(CadenceType type) {
  switch (type) {
    case CadenceType::Perfect:
    case CadenceType::PicardyThird:
      // Strict: small leaps, prefer standard suspensions for strong closure.
      return {/*max_leap_semitones=*/4, /*prefer_4_3_resolution=*/true,
              /*prefer_7_6_resolution=*/true};
    case CadenceType::Half:
      // Moderate: allow slightly larger leaps since resolution is incomplete.
      return {/*max_leap_semitones=*/5, /*prefer_4_3_resolution=*/true,
              /*prefer_7_6_resolution=*/false};
    case CadenceType::Deceptive:
      // Relaxed: inner voices need freedom to accommodate the unexpected bass.
      return {/*max_leap_semitones=*/5, /*prefer_4_3_resolution=*/false,
              /*prefer_7_6_resolution=*/true};
    case CadenceType::Phrygian:
      // Moderate: characteristic 7-6 resolution is essential for the idiom.
      return {/*max_leap_semitones=*/4, /*prefer_4_3_resolution=*/false,
              /*prefer_7_6_resolution=*/true};
    case CadenceType::Plagal:
      // Strict: amen cadence requires smooth inner voice motion.
      return {/*max_leap_semitones=*/3, /*prefer_4_3_resolution=*/true,
              /*prefer_7_6_resolution=*/false};
  }
  // Fallback for unknown types (should not be reached with valid CadenceType).
  return {/*max_leap_semitones=*/4, /*prefer_4_3_resolution=*/true,
          /*prefer_7_6_resolution=*/true};
}

/// @brief Get cadence approaches for a specific cadence type.
///
/// Returns a pointer to the first matching CadenceApproach in the aggregate
/// table and the count of approaches for that type. Iterates the constexpr
/// table at runtime (12 entries, trivial cost).
///
/// @param type The cadence type to look up.
/// @return Pair of (pointer to first approach, count). Returns (nullptr, 0)
///         if no approaches are defined for the type.
inline std::pair<const CadenceApproach*, size_t> getCadenceApproaches(CadenceType type) {
  const CadenceApproach* first = nullptr;
  size_t count = 0;
  for (size_t idx = 0; idx < kCadenceApproachCount; ++idx) {
    if (kCadenceApproaches[idx].type == type) {
      if (first == nullptr) {
        first = &kCadenceApproaches[idx];
      }
      ++count;
    }
  }
  return {first, count};
}

}  // namespace bach

#endif  // BACH_FUGUE_CADENCE_VOCABULARY_H
