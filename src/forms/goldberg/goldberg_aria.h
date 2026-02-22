// Goldberg Aria (Sarabande) generator for Var 0 and Var 31 (da capo).

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_ARIA_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_ARIA_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_aria_theme.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Result of aria generation.
struct AriaResult {
  std::vector<NoteEvent> melody_notes;  ///< Upper voice (GoldbergAria source).
  std::vector<NoteEvent> bass_notes;    ///< Bass voice (GoldbergBass source).
  AriaTheme theme;                      ///< Generated theme for grid write-back.
  bool success = false;
};

/// @brief Generates the Goldberg Aria (Sarabande) -- Var 0 and Var 31 (da capo).
///
/// 2 voices: melody + bass, faithful to the 32-bar structural grid.
/// Melody is generated via 2-layer scoring (Kern + Surface) then enriched
/// with Baroque ornaments. Bass line uses the structural grid's
/// primary_pitch from StructuralBassMotion.
class AriaGenerator {
 public:
  /// @brief Generate the Aria.
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature (G major for standard Goldberg).
  /// @param time_sig Time signature (3/4 for Sarabande).
  /// @param seed Random seed for deterministic generation.
  /// @return AriaResult with melody, bass notes, and generated theme.
  AriaResult generate(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint32_t seed) const;

  /// @brief Create da capo (Var 31) by offsetting Var 0 notes.
  /// @param original The original Var 0 AriaResult.
  /// @param tick_offset Tick offset to apply to all notes.
  /// @return New AriaResult with offset notes.
  static AriaResult createDaCapo(
      const AriaResult& original,
      Tick tick_offset);

 private:
  /// @brief Generate bass line from structural grid.
  std::vector<NoteEvent> generateBassLine(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      std::mt19937& rng) const;

  /// @brief Apply ornaments to melody notes with phrase-dependent density.
  void applyOrnaments(
      std::vector<NoteEvent>& notes,
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      std::mt19937& rng) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_ARIA_H
