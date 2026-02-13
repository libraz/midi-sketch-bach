// 3-voice canon generator for Goldberg Variations (Dux + Comes + Free Bass).

#ifndef BACH_FORMS_GOLDBERG_CANON_CANON_GENERATOR_H
#define BACH_FORMS_GOLDBERG_CANON_CANON_GENERATOR_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/canon/canon_types.h"
#include "forms/goldberg/canon/dux_buffer.h"
#include "forms/goldberg/goldberg_soggetto.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {

/// @brief Result of canon generation.
///
/// Contains the three voices (dux, comes, free bass), diagnostic backtrack
/// count, and a success flag. All note events carry correct BachNoteSource
/// provenance.
struct CanonResult {
  std::vector<NoteEvent> dux_notes;
  std::vector<NoteEvent> comes_notes;
  std::vector<NoteEvent> bass_notes;
  int backtrack_count = 0;  ///< Diagnostic: local retries used.
  bool success = false;
};

/// @brief Generates 3-voice accompanied canons (Dux + Comes + Free Bass).
///
/// Uses beat-by-beat forward generation with DuxBuffer for bidirectional
/// constraints. Each canon variation spans 32 bars in the Goldberg structural
/// grid.
///
/// The 5-phase algorithm:
///   Phase 0: Setup (state, buffer, soggetto skeleton).
///   Phase 1: Dux-only period (beat 0 to delay-1).
///   Phase 2: Three-voice canon (beat delay to total_beats).
///   Phase 3: Comes tail (after dux stops, remaining comes derivation).
class CanonGenerator {
 public:
  /// @brief Generate a complete canon variation.
  /// @param spec Canon specification (interval, transform, key, delay).
  /// @param grid 32-bar structural grid for harmonic/phrase alignment.
  /// @param time_sig Time signature (typically 3/4 for BWV 988).
  /// @param seed Random seed for deterministic generation.
  /// @return CanonResult with dux, comes, bass notes and success status.
  CanonResult generate(const CanonSpec& spec,
                       const GoldbergStructuralGrid& grid,
                       const TimeSignature& time_sig,
                       uint32_t seed) const;

 private:
  /// @brief Score a candidate dux pitch considering multiple constraints.
  /// @param candidate Candidate MIDI pitch.
  /// @param prev_dux_pitch Previous dux pitch for melodic quality scoring.
  /// @param buffer DuxBuffer for forward constraint preview.
  /// @param current_beat Current beat index (0-based).
  /// @param grid Structural grid for phrase/tension lookup.
  /// @param bar_info Structural bar info for current bar.
  /// @param strength Metrical strength of current beat.
  /// @param comes_pitch Active comes pitch (0 if no comes active).
  /// @param bass_pitch Active bass pitch (0 if no bass active).
  /// @param character Subject character for leap limits.
  /// @return Weighted score (higher = better candidate).
  float scoreDuxCandidate(uint8_t candidate,
                          uint8_t prev_dux_pitch,
                          const DuxBuffer& buffer,
                          int current_beat,
                          const GoldbergStructuralGrid& grid,
                          const StructuralBarInfo& bar_info,
                          MetricalStrength strength,
                          uint8_t comes_pitch,
                          uint8_t bass_pitch,
                          SubjectCharacter character) const;

  /// @brief Generate a free bass note aligned to structural grid.
  /// @param bar_info Structural bar info providing bass target pitch.
  /// @param tick Start tick for the bass note.
  /// @param dur Duration of the bass note.
  /// @param key Key signature for consonance checking.
  /// @param dux_pitch Current dux pitch for consonance validation.
  /// @param comes_pitch Current comes pitch for consonance validation.
  /// @param rng RNG for minor variation.
  /// @return NoteEvent with BachNoteSource::CanonFreeBass.
  NoteEvent generateBassNote(const StructuralBarInfo& bar_info,
                             Tick tick,
                             Tick dur,
                             const KeySignature& key,
                             uint8_t dux_pitch,
                             uint8_t comes_pitch,
                             std::mt19937& rng) const;

  /// @brief Select the best dux pitch from scale-degree candidates.
  /// @param candidates Vector of candidate MIDI pitches.
  /// @param prev_dux_pitch Previous dux pitch.
  /// @param buffer DuxBuffer for forward constraint checks.
  /// @param current_beat Current beat index.
  /// @param grid Structural grid.
  /// @param bar_info Current bar's structural info.
  /// @param strength Beat metrical strength.
  /// @param comes_pitch Active comes pitch (0 if none).
  /// @param bass_pitch Active bass pitch (0 if none).
  /// @param character Subject character for scoring parameters.
  /// @param rng RNG for tie-breaking perturbation.
  /// @return Best candidate pitch.
  uint8_t selectBestDuxPitch(const std::vector<uint8_t>& candidates,
                             uint8_t prev_dux_pitch,
                             const DuxBuffer& buffer,
                             int current_beat,
                             const GoldbergStructuralGrid& grid,
                             const StructuralBarInfo& bar_info,
                             MetricalStrength strength,
                             uint8_t comes_pitch,
                             uint8_t bass_pitch,
                             SubjectCharacter character,
                             std::mt19937& rng) const;

  /// @brief Check if a pitch class is a chord tone of the bar's harmony.
  /// @param pitch_class Pitch class (0-11).
  /// @param bar_info Structural bar info with chord degree.
  /// @param key Key signature for chord construction.
  /// @return True if pitch class matches root, 3rd, or 5th of the chord.
  bool isBarChordTone(int pitch_class,
                      const StructuralBarInfo& bar_info,
                      const KeySignature& key) const;

  /// @brief Build candidate pitches from scale degrees near a reference pitch.
  /// @param ref_pitch Reference MIDI pitch (previous dux note).
  /// @param key Key signature.
  /// @param is_minor Whether key is minor.
  /// @param max_leap Maximum leap interval in semitones.
  /// @return Vector of candidate MIDI pitches within range.
  std::vector<uint8_t> buildCandidates(uint8_t ref_pitch,
                                       const KeySignature& key,
                                       bool is_minor,
                                       int max_leap) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_CANON_CANON_GENERATOR_H
