// Cadence detection and insertion for fugue and fantasia generation.
//
// Detects stretches lacking harmonic tension release (leading-tone -> tonic
// resolution) and inserts minimal cadential formulas where needed. This avoids
// mechanical fixed-interval cadence placement by scanning for existing cadential
// activity first and only inserting when absent.

#ifndef BACH_FUGUE_CADENCE_INSERTION_H
#define BACH_FUGUE_CADENCE_INSERTION_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_structure.h"

namespace bach {

/// @brief Result of scanning for cadence-deficient stretches.
struct CadenceDeficiency {
  Tick region_start = 0;   ///< Start tick of the deficient region.
  Tick region_end = 0;     ///< End tick of the deficient region.
  Tick insertion_tick = 0;  ///< Recommended tick for cadence insertion.
};

/// @brief Configuration for cadence detection thresholds.
struct CadenceDetectionConfig {
  /// Maximum bars without cadential resolution before flagging as deficient.
  /// Fugue default: 16 bars. Fantasia (stylus phantasticus): 24 bars.
  int max_bars_without_cadence = 16;

  /// Window size in bars for scanning (default: 8 bars).
  int scan_window_bars = 8;

  /// Probability of using a deceptive cadence (V->vi) instead of perfect (V->I).
  /// Default: 0.20 (~20% of insertions).
  float deceptive_cadence_probability = 0.20f;
};

/// @brief Check if notes contain a leading-tone -> tonic resolution near the given tick.
///
/// Searches for a pitch with pitch_class == (tonic + 11) % 12 resolving to a pitch
/// with pitch_class == tonic within a window of +/- 2 beats around the specified tick.
/// The leading tone must precede the tonic in time for the resolution to be valid.
///
/// @param notes The note collection to search.
/// @param tick Center tick of the search window.
/// @param key The key (pitch class of tonic) to check against.
/// @return True if a leading-tone -> tonic resolution is found.
bool hasCadentialResolution(const std::vector<NoteEvent>& notes, Tick tick, Key key);

/// @brief Check if a tick falls within a subject entry region.
///
/// Scans the fugue structure for MiddleEntry, Exposition, or Stretto sections
/// that overlap the given tick position.
///
/// @param structure The fugue structure with section boundaries.
/// @param tick The tick position to check.
/// @return True if the tick falls within a subject entry section.
bool isInSubjectEntry(const FugueStructure& structure, Tick tick);

/// @brief Scan notes for stretches lacking cadential activity.
///
/// Scans through the note collection in windows defined by the config. For each
/// window that lacks a leading-tone -> tonic resolution AND does not overlap with
/// a subject entry (which provides its own structural articulation), the region
/// is flagged as cadence-deficient.
///
/// @param notes All generated notes.
/// @param structure The fugue structure (for subject entry detection).
/// @param key The current key.
/// @param total_duration Total duration of the piece in ticks.
/// @param config Detection configuration (thresholds).
/// @return Vector of cadence-deficient regions with recommended insertion ticks.
std::vector<CadenceDeficiency> detectCadenceDeficiencies(
    const std::vector<NoteEvent>& notes,
    const FugueStructure& structure,
    Key key,
    Tick total_duration,
    const CadenceDetectionConfig& config = {});

/// @brief Insert minimal cadential formulas at detected deficiency points.
///
/// For each deficiency, inserts 2-3 bass notes at the recommended insertion tick:
///   - Perfect cadence (V->I): dominant pitch 1 beat before boundary, tonic at boundary.
///   - Deceptive cadence (V->vi): dominant pitch 1 beat before, sixth degree at boundary.
///
/// Deceptive cadences are used with probability config.deceptive_cadence_probability
/// using the deterministic PRNG seeded from the provided seed.
///
/// Inserted notes use BachNoteSource::EpisodeMaterial for Flexible protection level.
///
/// @param notes Note collection to insert into (modified in place).
/// @param deficiencies Detected deficiency regions.
/// @param key The tonic key.
/// @param is_minor True if the key is minor.
/// @param bass_voice Voice ID for bass notes (typically num_voices - 1).
/// @param num_voices Total number of voices.
/// @param seed Deterministic PRNG seed.
/// @param config Detection configuration (for deceptive cadence probability).
/// @return Number of cadences inserted.
int insertCadentialFormulas(
    std::vector<NoteEvent>& notes,
    const std::vector<CadenceDeficiency>& deficiencies,
    Key key,
    bool is_minor,
    VoiceId bass_voice,
    uint8_t num_voices,
    uint32_t seed,
    const CadenceDetectionConfig& config = {});

/// @brief Combined detection and insertion for fugue generation.
///
/// Convenience function that runs detectCadenceDeficiencies followed by
/// insertCadentialFormulas. Returns the number of cadences inserted.
///
/// @param notes All generated notes (modified in place).
/// @param structure The fugue structure.
/// @param key The tonic key.
/// @param is_minor True if the key is minor.
/// @param bass_voice Voice ID for bass insertions.
/// @param num_voices Total number of voices.
/// @param total_duration Total piece duration in ticks.
/// @param seed Deterministic PRNG seed.
/// @param config Detection configuration.
/// @return Number of cadences inserted.
int ensureCadentialCoverage(
    std::vector<NoteEvent>& notes,
    const FugueStructure& structure,
    Key key,
    bool is_minor,
    VoiceId bass_voice,
    uint8_t num_voices,
    Tick total_duration,
    uint32_t seed,
    const CadenceDetectionConfig& config = {});

}  // namespace bach

#endif  // BACH_FUGUE_CADENCE_INSERTION_H
