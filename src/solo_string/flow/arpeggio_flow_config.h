// Configuration types for the Solo String Flow arpeggio engine (BWV1007 style).

#ifndef BACH_SOLO_STRING_FLOW_ARPEGGIO_FLOW_CONFIG_H
#define BACH_SOLO_STRING_FLOW_ARPEGGIO_FLOW_CONFIG_H

#include <cstdint>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Section identifier within a flow piece.
using SectionId = uint8_t;

/// @brief Configuration for the global arc (config-fixed, seed-independent).
///
/// The global arc defines which ArcPhase each section belongs to.
/// Once set, the arc is immutable for the lifetime of the generation pass.
/// Rules: phase order is Ascent -> Peak -> Descent (no reversal), Peak exactly 1 section.
struct GlobalArcConfig {
  /// ArcPhase assignment per section. Fixed at config time, never changes.
  /// Each pair maps (SectionId -> ArcPhase). Must satisfy:
  /// - Monotonic order: Ascent sections before Peak before Descent
  /// - Exactly one section assigned to Peak
  std::vector<std::pair<SectionId, ArcPhase>> phase_assignment;
};

/// @brief Cadence configuration for the final bars of a flow piece.
///
/// Controls how the piece winds down to its final cadence, including
/// register constraints, rhythmic simplification, and open-string preference.
struct CadenceConfig {
  int cadence_bars = 8;               // Number of bars for cadential section
  float open_string_bias = 0.7f;      // Preference for open strings [0.0, 1.0]
  bool restrict_high_register = true; // Avoid high register in cadence
  float rhythm_simplification = 0.3f; // Amount of rhythmic simplification [0.0, 1.0]
};

/// @brief Full configuration for the harmonic arpeggio engine (Flow system).
///
/// This is the top-level config struct for generating BWV1007-style cello preludes.
/// The global arc and cadence are config-fixed (seed-independent).
struct ArpeggioFlowConfig {
  KeySignature key = {Key::D, true};  // Default: D minor
  uint16_t bpm = 66;
  uint32_t seed = 0;                  // 0 = auto (time-based)
  InstrumentType instrument = InstrumentType::Cello;
  int num_sections = 6;               // Typical: 6 sections
  int bars_per_section = 4;           // Each section is 4 bars
  GlobalArcConfig arc;
  CadenceConfig cadence;
};

/// @brief Validate a GlobalArcConfig for structural correctness.
///
/// Checks that:
/// - ArcPhase order is Ascent -> Peak -> Descent (monotonic, no reversal)
/// - Exactly one section is assigned to Peak
/// - At least one assignment exists
///
/// @param config The GlobalArcConfig to validate.
/// @return true if the config satisfies all constraints, false otherwise.
bool validateGlobalArcConfig(const GlobalArcConfig& config);

/// @brief Create a default GlobalArcConfig for a given number of sections.
///
/// Places Peak at approximately 60-70% position (ceil(num_sections * 0.65)).
/// This is config-fixed and seed-independent: the arc structure is a design
/// decision, not a random variable.
///
/// @param num_sections Total number of sections in the piece (must be >= 3).
/// @return A valid GlobalArcConfig with default phase assignment.
///         Returns empty config if num_sections < 3.
GlobalArcConfig createDefaultArcConfig(int num_sections);

}  // namespace bach

#endif  // BACH_SOLO_STRING_FLOW_ARPEGGIO_FLOW_CONFIG_H
