// Per-phase register envelope for controlling voice range evolution.
// Used by exposition.cpp, middle_entry.cpp, stretto.cpp.

#ifndef BACH_CORE_FORM_PROFILE_H
#define BACH_CORE_FORM_PROFILE_H

#include "core/basic_types.h"

namespace bach {

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
      return {0.65f, 0.85f, 1.0f, 0.75f};
    case FormType::Passacaglia:
      return {0.70f, 0.90f, 1.0f, 0.80f};
    case FormType::TrioSonata:
      return {0.75f, 0.85f, 0.95f, 0.80f};
    case FormType::ChoralePrelude:
      return {0.80f, 0.85f, 0.90f, 0.85f};
    default:
      return {0.70f, 0.85f, 1.0f, 0.80f};
  }
}

}  // namespace bach

#endif  // BACH_CORE_FORM_PROFILE_H
