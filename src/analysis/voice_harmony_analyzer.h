// Read-only voice-harmony alignment analysis for generated tracks.

#ifndef BACH_ANALYSIS_VOICE_HARMONY_ANALYZER_H
#define BACH_ANALYSIS_VOICE_HARMONY_ANALYZER_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

/// @brief Report from voice-harmony alignment analysis.
///
/// This is a READ-ONLY analysis product. It never feeds back into
/// generation and never modifies any notes or timeline events.
struct VoiceHarmonyReport {
  float chord_tone_coverage = 0.0f;    ///< Ratio of notes that are chord tones [0,1].
  float voice_leading_quality = 0.0f;  ///< Ratio of stepwise motion [0,1].
  float contrary_motion_ratio = 0.0f;  ///< Ratio of contrary motion between voices [0,1].
  int suspension_count = 0;            ///< Number of detected suspensions.
  int total_notes = 0;                 ///< Total notes analyzed.
  int chord_tone_notes = 0;            ///< Notes that are chord tones.
  int stepwise_notes = 0;              ///< Notes reached by step (1-2 semitones).
  std::vector<std::string> observations;  ///< Human-readable analysis notes.
};

/// @brief Analyze voice-harmony alignment in generated tracks (read-only).
///
/// Examines how well the generated voices align with the harmonic timeline.
/// This is a pure analysis function that NEVER modifies its inputs.
///
/// @param tracks The generated tracks to analyze.
/// @param timeline The harmonic timeline used during generation.
/// @return VoiceHarmonyReport with quality metrics.
VoiceHarmonyReport analyzeVoiceHarmony(const std::vector<Track>& tracks,
                                       const HarmonicTimeline& timeline);

/// @brief Analyze a single track's harmony alignment.
/// @param notes Notes from one track.
/// @param timeline Harmonic timeline.
/// @return Partial report for this track.
VoiceHarmonyReport analyzeTrackHarmony(const std::vector<NoteEvent>& notes,
                                       const HarmonicTimeline& timeline);

/// @brief Count suspensions in a note sequence.
///
/// A suspension is detected when:
///   1. A note is held from the previous beat (tied or repeated pitch)
///   2. The held note is dissonant with the current chord
///   3. The next note resolves downward by step
///
/// @param notes Notes to analyze.
/// @param timeline Harmonic timeline.
/// @return Number of suspensions detected.
int countSuspensions(const std::vector<NoteEvent>& notes, const HarmonicTimeline& timeline);

}  // namespace bach

#endif  // BACH_ANALYSIS_VOICE_HARMONY_ANALYZER_H
