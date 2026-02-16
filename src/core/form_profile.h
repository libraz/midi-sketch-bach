// Form-level generation profiles calibrated from Bach reference data.
// Provides form-specific targets for interval distribution, rhythm density,
// and dissonance policy.

#ifndef BACH_CORE_FORM_PROFILE_H
#define BACH_CORE_FORM_PROFILE_H

#include "core/basic_types.h"

namespace bach {

/// @brief Policy for dissonance handling in vertical coordination.
///
/// Counterpoint dissonance is governed by placement rules, not ratio targets.
/// Suspensions are validated by checking preparation, resolution direction,
/// and stepwise resolution — not merely "allowed."
struct DissonancePolicy {
  bool allow_passing_dissonance = true;    ///< Allow passing tones on weak beats.
  bool allow_neighbor_dissonance = true;   ///< Allow neighbor tones on weak beats.

  /// If true, strong-beat dissonances must satisfy all 3 suspension conditions:
  ///   1. Preparation: same pitch sounding on the previous beat.
  ///   2. Resolution direction: downward resolution preferred.
  ///   3. Stepwise resolution: resolution by 2nd (not leap).
  /// If conditions are not met, the dissonance is rejected.
  /// If false, strong-beat dissonance is rejected outright.
  bool require_valid_suspension = true;

  Tick max_dissonance_overlap = 240;  ///< Max overlap for passing dissonance (default: 8th).
  bool allow_weak_beat_tritone = false;  ///< Allow tritone on weak beats.
};

/// @brief Form-level generation profile calibrated from Bach reference data.
///
/// Target values are used for JSD monitoring and weak scoring bias.
/// They do NOT directly modify generation probabilities (those are
/// controlled by VoiceProfile to avoid conflicts).
struct FormProfile {
  float target_stepwise_ratio;   ///< JSD monitoring + weak scoring bias.
  float target_avg_interval;     ///< JSD monitoring (not used for generation).
  float target_notes_per_bar;    ///< Density steering target.
  DissonancePolicy dissonance_policy;
};

/// @brief Get the canonical FormProfile for a given form type.
///
/// Values are derived from bach-reference MCP category summaries
/// across 270 reference works.
inline FormProfile getFormProfile(FormType form) {
  switch (form) {
    case FormType::Fugue:
      return {0.45f, 3.34f, 23.5f, {true, true, true, 240, false}};
    case FormType::PreludeAndFugue:
      return {0.49f, 3.18f, 26.8f, {true, true, true, 240, false}};
    case FormType::TrioSonata:
      return {0.58f, 2.83f, 34.5f, {true, true, true, 240, false}};
    case FormType::ChoralePrelude:
      return {0.55f, 3.01f, 25.3f, {true, true, true, 240, false}};
    case FormType::ToccataAndFugue:
      return {0.45f, 3.34f, 23.5f, {true, true, true, 240, false}};
    case FormType::Passacaglia:
      return {0.57f, 2.47f, 31.9f, {true, true, true, 240, false}};
    case FormType::FantasiaAndFugue:
      return {0.45f, 3.34f, 23.5f, {true, true, true, 240, false}};
    case FormType::CelloPrelude:
      return {0.55f, 2.88f, 14.5f, {true, true, false, 240, true}};
    case FormType::Chaconne:
      return {0.51f, 3.18f, 19.0f, {true, true, false, 240, true}};
    case FormType::GoldbergVariations:
      return {0.53f, 3.20f, 23.5f, {true, true, true, 240, false}};
    default:
      return {0.50f, 3.20f, 23.5f, {true, true, true, 240, false}};
  }
}

/// Register envelope contraction direction.
enum class ContractionTarget : uint8_t {
  Centered,     ///< Converge toward register center.
  UpperBiased,  ///< Converge upward.
  LowerBiased,  ///< Converge downward.
};

/// Per-phase register envelope for controlling voice range evolution.
///
/// Ratios specify the fraction of the full voice range that should be
/// actively used in each structural phase. A ratio of 1.0 means full
/// range is available; 0.65 means only 65% of the range is used.
/// Calibrated from Bach reference data (BWV578, WTC1 category averages).
struct RegisterEnvelope {
  float opening_range_ratio = 0.65f;   ///< Exposition: restricted range for clarity.
  float middle_range_ratio = 0.85f;    ///< Development: expanding range.
  float climax_range_ratio = 1.0f;     ///< Stretto/climax: full range available.
  float closing_range_ratio = 0.75f;   ///< Coda: contracting for resolution.
  bool require_new_high_at_climax = true;  ///< Climax must exceed opening max pitch.
  ContractionTarget contraction = ContractionTarget::Centered;
};

/// Get the canonical RegisterEnvelope for a given form type.
///
/// Values derived from bach-reference MCP register evolution analysis
/// across organ_fugue (8 works) and wtc1 (48 works) categories.
inline RegisterEnvelope getRegisterEnvelope(FormType form) {
  switch (form) {
    case FormType::Fugue:
    case FormType::PreludeAndFugue:
    case FormType::ToccataAndFugue:
    case FormType::FantasiaAndFugue:
      return {0.65f, 0.85f, 1.0f, 0.75f, true, ContractionTarget::Centered};
    case FormType::Passacaglia:
      return {0.70f, 0.90f, 1.0f, 0.80f, true, ContractionTarget::LowerBiased};
    case FormType::TrioSonata:
      return {0.75f, 0.85f, 0.95f, 0.80f, false, ContractionTarget::Centered};
    case FormType::ChoralePrelude:
      return {0.80f, 0.85f, 0.90f, 0.85f, false, ContractionTarget::UpperBiased};
    default:
      return {0.70f, 0.85f, 1.0f, 0.80f, false, ContractionTarget::Centered};
  }
}

}  // namespace bach

#endif  // BACH_CORE_FORM_PROFILE_H
