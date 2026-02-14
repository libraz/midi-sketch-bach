// Toccata free section generator for organ toccata (BWV 565 style).

#ifndef BACH_FORMS_TOCCATA_H
#define BACH_FORMS_TOCCATA_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief Configuration for toccata free section (BWV 565 style).
struct ToccataConfig {
  ToccataArchetype archetype = ToccataArchetype::Dramaticus;
  KeySignature key = {Key::D, true};  ///< D minor default (BWV 565).
  uint16_t bpm = 80;
  uint32_t seed = 42;
  uint8_t num_voices = 3;
  int total_bars = 24;                ///< Total toccata length in bars.
  bool enable_picardy = true;         ///< Apply Picardy third in minor keys.
};;;

/// @brief Result of toccata free section generation.
/// @brief Section boundary within a toccata.
struct ToccataSectionBoundary {
  ToccataSectionId id;
  Tick start = 0;
  Tick end = 0;
};

struct ToccataResult {
  std::vector<Track> tracks;
  HarmonicTimeline timeline;
  Tick total_duration_ticks = 0;
  bool success = false;
  std::string error_message;
  ToccataArchetype archetype = ToccataArchetype::Dramaticus;
  std::vector<ToccataSectionBoundary> sections;
  std::vector<ToccataSectionBoundary> phases;  // 8-phase (Dramaticus), empty for others.

  // Legacy section boundary fields (always populated for backward compat).
  // Non-Dramaticus: opening=sections[0], recit=sections[1], drive=sections.back().
  Tick opening_start = 0;
  Tick opening_end = 0;
  Tick recit_start = 0;
  Tick recit_end = 0;
  Tick drive_start = 0;
  Tick drive_end = 0;
};;

/// @brief Generate a toccata free section (BWV 565 opening style).
///
/// Structure: Dramatic, virtuosic free section without strict counterpoint.
/// Features scale runs, arpeggios, pedal points, and dramatic pauses.
/// Energy curve: high -> low -> high (dramatic U-shape).
///
/// The section is divided into three segments:
///   - Opening dramatic gesture (25%): Fast scale runs on Manual I
///   - Recitative/exploration (50%): Alternating free passages on Manual II
///   - Drive to cadence (25%): Building energy, all manuals active
///
/// No counterpoint rules applied (free form).
///
/// @param config Toccata configuration.
/// @return ToccataResult with generated tracks.
ToccataResult generateToccata(const ToccataConfig& config);

// Archetype-specific generators (called via dispatcher above).
ToccataResult generateDramaticusToccata(const ToccataConfig& config);
ToccataResult generatePerpetuusToccata(const ToccataConfig& config);
ToccataResult generateConcertatoToccata(const ToccataConfig& config);
ToccataResult generateSectionalisToccata(const ToccataConfig& config);

}  // namespace bach

#endif  // BACH_FORMS_TOCCATA_H
