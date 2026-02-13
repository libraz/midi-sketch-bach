// Quodlibet variation generator for Goldberg Variations (Var 30).

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_QUODLIBET_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_QUODLIBET_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// Grid-vs-melody clash policy for Quodlibet.
/// Controls how aggressively melody notes are snapped to grid harmony.
struct QuodlibetClashPolicy {
  static constexpr int kStrongBeatMaxClash = 0;         ///< Semitones allowed on strong beats.
  static constexpr int kWeakBeatMaxClash = 2;            ///< Semitones allowed on weak beats.
  static constexpr float kWeakBeatMaxDuration = 1.0f;    ///< Max beats for weak-beat clash.
  static constexpr float kClashPenaltyPerSemitone = 1.5f;  ///< Penalty weight per semitone.
};

/// @brief Result of Quodlibet variation generation.
struct QuodlibetResult {
  std::vector<NoteEvent> notes;
  bool success = false;
};

/// @brief Generates the Quodlibet variation (Var 30).
///
/// Combines two folk melodies ("Ich bin so lang nicht bei dir g'west" and
/// "Kraut und Rueben haben mich vertrieben") over the Goldberg structural grid.
/// The two melodies are placed in separate voices; in the second half (bars 17-32)
/// the melodies swap voices for variety. A structural bass line from the grid
/// provides harmonic foundation, and binary repeats are applied.
///
/// Humor meets structure: the folk melodies are adjusted to fit the grid's
/// harmony on strong beats, while their contour is preserved as much as possible.
class QuodlibetGenerator {
 public:
  /// @brief Generate the complete Quodlibet variation.
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature (typically G major).
  /// @param time_sig Time signature (typically 3/4).
  /// @param seed Random seed for deterministic generation.
  /// @return QuodlibetResult with generated notes and success status.
  QuodlibetResult generate(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint32_t seed) const;

 private:
  /// @brief Place a folk melody onto the grid, adjusting pitches for harmonic alignment.
  ///
  /// For each melody note, determines which bar it falls in and checks the bar's
  /// harmony. On strong beats, melody pitches are snapped to chord tones. On weak
  /// beats, up to kWeakBeatMaxClash semitones of clash are tolerated. Melody contour
  /// (relative pitch direction) is preserved even when adjusting pitches.
  ///
  /// @param melody_pitches Array of MIDI pitches for the folk melody.
  /// @param melody_durations Array of durations (ticks) for each melody note.
  /// @param melody_length Number of notes in the melody arrays.
  /// @param start_tick Tick offset where the melody begins.
  /// @param bar_count Number of bars to fill with the melody (repeating as needed).
  /// @param grid The structural grid for harmonic alignment.
  /// @param key Key signature for scale context.
  /// @param time_sig Time signature for beat calculation.
  /// @param voice Voice index for the melody placement.
  /// @param rng Random engine for minor variation.
  /// @return Vector of NoteEvents with QuodlibetMelody source.
  std::vector<NoteEvent> placeMelodyOnGrid(
      const uint8_t* melody_pitches,
      const Tick* melody_durations,
      int melody_length,
      Tick start_tick,
      int bar_count,
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint8_t voice,
      std::mt19937& rng) const;

  /// @brief Generate structural bass line from the grid.
  /// @param grid The structural grid.
  /// @param key Key signature.
  /// @param time_sig Time signature.
  /// @return Vector of bass NoteEvents with GoldbergBass source.
  std::vector<NoteEvent> generateBassLine(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig) const;

  /// @brief Snap a pitch to the nearest chord tone of the bar's harmony.
  /// @param pitch Original MIDI pitch.
  /// @param bar_info Structural bar info containing chord information.
  /// @param key Key signature for chord tone calculation.
  /// @return Adjusted MIDI pitch (nearest chord tone).
  uint8_t snapToChordTone(
      uint8_t pitch,
      const StructuralBarInfo& bar_info,
      const KeySignature& key) const;

  /// @brief Validate cadence alignment between folk melody and grid.
  /// @param notes Generated melody notes to check.
  /// @param grid The structural grid with cadence positions.
  /// @param time_sig Time signature for bar calculation.
  /// @return True if cadence positions align with melody phrase endings.
  bool validateCadenceAlignment(
      const std::vector<NoteEvent>& notes,
      const GoldbergStructuralGrid& grid,
      const TimeSignature& time_sig) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_QUODLIBET_H
