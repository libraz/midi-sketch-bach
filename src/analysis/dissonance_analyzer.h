// Dissonance analysis pipeline -- 4-phase detection for Organ and Solo String systems.

#ifndef BACH_ANALYSIS_DISSONANCE_ANALYZER_H
#define BACH_ANALYSIS_DISSONANCE_ANALYZER_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// Severity level for detected dissonance events.
enum class DissonanceSeverity : uint8_t {
  Low,     ///< Acceptable in context (passing tone, weak beat).
  Medium,  ///< Notable but not critical.
  High     ///< Likely unintentional, needs attention.
};

/// @brief Convert DissonanceSeverity to human-readable string.
const char* dissonanceSeverityToString(DissonanceSeverity severity);

/// The four phases of dissonance detection.
enum class DissonanceType : uint8_t {
  SimultaneousClash,         ///< Phase 1: Beat-by-beat interval clash (Organ only).
  NonChordTone,              ///< Phase 2: Pitch not in current chord.
  SustainedOverChordChange,  ///< Phase 3: Held note clashes after chord change (Organ only).
  NonDiatonicNote            ///< Phase 4: Pitch outside the diatonic scale.
};

/// @brief Convert DissonanceType to human-readable string.
const char* dissonanceTypeToString(DissonanceType type);

/// A single detected dissonance event.
struct DissonanceEvent {
  DissonanceType type = DissonanceType::SimultaneousClash;
  DissonanceSeverity severity = DissonanceSeverity::Medium;
  Tick tick = 0;
  uint32_t bar = 0;    ///< 1-based bar number.
  uint8_t beat = 0;    ///< 1-based beat number within bar.
  uint8_t pitch = 0;
  uint8_t other_pitch = 0;  ///< Second pitch for clash events, 0 if N/A.
  VoiceId voice_a = 0;
  VoiceId voice_b = 0;
  int interval = 0;         ///< Interval in semitones (for clash events).
  std::string description;
};

/// Summary statistics for a dissonance analysis.
struct DissonanceAnalysisSummary {
  uint32_t total = 0;
  uint32_t high_count = 0;
  uint32_t medium_count = 0;
  uint32_t low_count = 0;
  uint32_t simultaneous_clash_count = 0;
  uint32_t non_chord_tone_count = 0;
  uint32_t sustained_over_chord_change_count = 0;
  uint32_t non_diatonic_note_count = 0;
  float density_per_beat = 0.0f;           ///< Events per beat (raw).
  float weighted_density_per_beat = 0.0f;  ///< Weighted: High=1.0, Medium=0.5, Low=0.0.
};

/// Complete result of a dissonance analysis pass.
struct DissonanceAnalysisResult {
  std::vector<DissonanceEvent> events;
  DissonanceAnalysisSummary summary;

  /// @brief Generate a human-readable text summary.
  std::string toTextSummary(const char* system_name, uint8_t num_voices) const;

  /// @brief Serialize to JSON string.
  std::string toJson() const;
};

// ---------------------------------------------------------------------------
// Phase detection functions (exposed for testing)
// ---------------------------------------------------------------------------

/// @brief Phase 1: Detect simultaneous interval clashes between voices.
/// @param notes All notes across all voices.
/// @param num_voices Number of distinct voices.
/// @return Detected clash events.
/// @note Organ system only -- scans each beat for dissonant intervals.
std::vector<DissonanceEvent> detectSimultaneousClashes(
    const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Phase 2: Detect non-chord tones against the harmonic timeline.
/// @param notes All notes (may span multiple voices or single voice).
/// @param timeline Harmonic timeline providing chord context.
/// @param generation_timeline Optional beat-resolution timeline from generation.
///        If provided, notes that are chord tones of this timeline are downgraded
///        to Low severity (dual-timeline fix for bar-vs-beat resolution mismatch).
/// @return Detected non-chord-tone events.
std::vector<DissonanceEvent> detectNonChordTones(
    const std::vector<NoteEvent>& notes, const HarmonicTimeline& timeline,
    const HarmonicTimeline* generation_timeline = nullptr);

/// @brief Phase 3: Detect notes sustained over a chord change that clash.
/// @param notes All notes across all voices.
/// @param num_voices Number of distinct voices.
/// @param timeline Harmonic timeline providing chord boundaries.
/// @return Detected sustained-clash events.
/// @note Organ system only -- checks held notes at chord boundaries.
std::vector<DissonanceEvent> detectSustainedOverChordChange(
    const std::vector<NoteEvent>& notes, uint8_t num_voices,
    const HarmonicTimeline& timeline);

/// @brief Phase 4: Detect non-diatonic notes (chromatic pitches).
/// @param notes All notes.
/// @param key_sig Key signature for diatonic context.
/// @return Detected non-diatonic events.
std::vector<DissonanceEvent> detectNonDiatonicNotes(
    const std::vector<NoteEvent>& notes, const KeySignature& key_sig);

// ---------------------------------------------------------------------------
// Orchestrator functions
// ---------------------------------------------------------------------------

/// @brief Run all 4 phases for Organ system.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @param timeline Harmonic timeline.
/// @param key_sig Key signature.
/// @param generation_timeline Optional beat-resolution timeline for dual-timeline
///        NCT downgrade. nullptr preserves backward-compatible single-timeline behavior.
/// @return Complete dissonance analysis result.
DissonanceAnalysisResult analyzeOrganDissonance(
    const std::vector<NoteEvent>& notes, uint8_t num_voices,
    const HarmonicTimeline& timeline, const KeySignature& key_sig,
    const HarmonicTimeline* generation_timeline = nullptr);

/// @brief Run phases 2 + 4 for Solo String system.
/// @param notes All notes (single melodic line).
/// @param timeline Harmonic timeline.
/// @param key_sig Key signature.
/// @return Complete dissonance analysis result.
DissonanceAnalysisResult analyzeSoloStringDissonance(
    const std::vector<NoteEvent>& notes,
    const HarmonicTimeline& timeline, const KeySignature& key_sig);

}  // namespace bach

#endif  // BACH_ANALYSIS_DISSONANCE_ANALYZER_H
